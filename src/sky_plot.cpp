#include "arctra/sky_plot.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace arctra {
namespace {

constexpr double pi = 3.14159265358979323846;

enum class PlotStyle { plain, focused, above_threshold, below_threshold };

struct Cell {
    char value{' '};
    PlotStyle style{PlotStyle::plain};
};

double degrees_to_radians(const double degrees) {
    return degrees * pi / 180.0;
}

std::size_t clamp_coordinate(const long value, const std::size_t limit) {
    if (value < 0) return 0;
    return std::min(static_cast<std::size_t>(value), limit - 1);
}

std::string_view colour_code(const PlotStyle style) {
    switch (style) {
    case PlotStyle::plain: return "\x1b[39m";
    case PlotStyle::focused: return "\x1b[93m";
    case PlotStyle::above_threshold: return "\x1b[32m";
    case PlotStyle::below_threshold: return "\x1b[90m";
    }
    return "\x1b[39m";
}

std::string styled_row(const std::vector<Cell>& row) {
    std::string result;
    PlotStyle previous = PlotStyle::plain;
    for (const auto& cell : row) {
        if (cell.style != previous) {
            result += colour_code(cell.style);
            previous = cell.style;
        }
        result += cell.value;
    }
    if (previous != PlotStyle::plain) result += "\x1b[39m";
    return result;
}

std::string format_range_km(const double slant_range_m) {
    const auto kilometres = std::to_string(std::llround(slant_range_m / 1000.0));
    std::string grouped;
    grouped.reserve(kilometres.size() + kilometres.size() / 3 + 3);
    for (std::size_t index = 0; index < kilometres.size(); ++index) {
        if (index > 0 && (kilometres.size() - index) % 3 == 0) grouped += ' ';
        grouped += kilometres[index];
    }
    return grouped + " km";
}

}  // namespace

SkyPlot::SkyPlot(const std::size_t width, const std::size_t height,
                 const double minimum_elevation_deg)
    : width_{width}, height_{height},
      minimum_elevation_deg_{std::clamp(minimum_elevation_deg, -89.0, 89.0)} {}

bool SkyPlot::drawable() const noexcept {
    return width_ >= minimum_width() && height_ >= minimum_height();
}

std::vector<std::string> SkyPlot::render(const std::span<const SkyPlotTarget> targets) const {
    if (!drawable()) return {};

    std::vector<std::vector<Cell>> canvas(height_ - 2, std::vector<Cell>(width_ - 2));
    const auto canvas_width = canvas.front().size();
    const auto canvas_height = canvas.size();
    const auto centre_x = static_cast<long>(canvas_width / 2);
    const auto centre_y = static_cast<long>(canvas_height / 2);
    const auto radius_y = std::max<long>(1, (static_cast<long>(canvas_height) - 3) / 2);
    const auto radius_x = std::max<long>(1, std::min((static_cast<long>(canvas_width) - 3) / 2,
                                                     radius_y * 2));

    const auto put = [&](const long x, const long y, const char value, const bool overwrite = false,
                         const PlotStyle style = PlotStyle::plain) {
        const auto column = clamp_coordinate(x, canvas_width);
        const auto row = clamp_coordinate(y, canvas_height);
        if (overwrite || canvas[row][column].value == ' ') canvas[row][column] = {value, style};
    };
    const auto point_on_ring = [&](const double azimuth_deg, const double radius, const char value) {
        const auto azimuth = degrees_to_radians(azimuth_deg);
        put(centre_x + std::lround(std::sin(azimuth) * radius * static_cast<double>(radius_x)),
            centre_y - std::lround(std::cos(azimuth) * radius * static_cast<double>(radius_y)), value);
    };

    for (int azimuth = 0; azimuth < 360; azimuth += 3) point_on_ring(azimuth, 1.0, '.');
    for (int azimuth = 0; azimuth < 360; azimuth += 5) point_on_ring(azimuth, 0.5, ':');
    put(centre_x, centre_y, '+', true);
    put(centre_x, centre_y - radius_y, 'N', true);
    put(centre_x + radius_x, centre_y, 'E', true);
    put(centre_x, centre_y + radius_y, 'S', true);
    put(centre_x - radius_x, centre_y, 'W', true);

    struct Marker {
        long x;
        long y;
        char symbol;
        PlotStyle style;
        std::string name;
        std::string range;
    };
    std::vector<Marker> markers;
    markers.reserve(targets.size());
    for (const auto& target : targets) {
        if (!std::isfinite(target.azimuth_deg) || !std::isfinite(target.elevation_deg)) continue;
        auto name = std::string{target.name};
        std::string range;
        if (target.above_minimum_elevation && std::isfinite(target.slant_range_m)) {
            range = format_range_km(target.slant_range_m);
        }
        const auto radius = std::clamp(
            (90.0 - target.elevation_deg) / (90.0 - minimum_elevation_deg_), 0.0, 1.0);
        const auto azimuth = degrees_to_radians(target.azimuth_deg);
        markers.push_back({centre_x + std::lround(std::sin(azimuth) * radius * static_cast<double>(radius_x)),
                           centre_y - std::lround(std::cos(azimuth) * radius * static_cast<double>(radius_y)),
                           target.focused ? '@' : (target.above_minimum_elevation ? '*' : 'o'),
                           target.focused ? PlotStyle::focused
                                          : (target.above_minimum_elevation
                                                 ? PlotStyle::above_threshold
                                                 : PlotStyle::below_threshold),
                           std::move(name), std::move(range)});
    }
    std::stable_sort(markers.begin(), markers.end(), [](const Marker& left, const Marker& right) {
        return left.style == PlotStyle::focused && right.style != PlotStyle::focused;
    });

    std::vector<std::vector<bool>> marker_cells(canvas_height,
                                                std::vector<bool>(canvas_width, false));
    for (const auto& marker : markers) {
        put(marker.x, marker.y, marker.symbol, true, marker.style);
        marker_cells[clamp_coordinate(marker.y, canvas_height)]
                    [clamp_coordinate(marker.x, canvas_width)] = true;
    }

    std::vector<std::vector<bool>> label_cells(canvas_height,
                                               std::vector<bool>(canvas_width, false));
    for (const auto& marker : markers) {
        if (marker.name.empty()) continue;
        const auto line_fits = [&](const std::string_view text, const long start, const long row) {
            if (start < 0 || row < 0 || row >= static_cast<long>(canvas_height) ||
                start + static_cast<long>(text.size()) > static_cast<long>(canvas_width)) {
                return false;
            }
            for (std::size_t index = 0; index < text.size(); ++index) {
                const auto column = static_cast<std::size_t>(start) + index;
                if (marker_cells[static_cast<std::size_t>(row)][column] ||
                    label_cells[static_cast<std::size_t>(row)][column]) return false;
            }
            return true;
        };
        const auto label_fits = [&](const long start, const long row) {
            return line_fits(marker.name, start, row) &&
                   (marker.range.empty() || line_fits(marker.range, start, row + 1));
        };
        const std::array<std::pair<long, long>, 6> candidates{{
            {marker.x + 2, marker.y},
            {marker.x - static_cast<long>(marker.name.size()) - 1, marker.y},
            {marker.x + 2, marker.y - 1},
            {marker.x - static_cast<long>(marker.name.size()) - 1, marker.y - 1},
            {marker.x + 2, marker.y - 2},
            {marker.x - static_cast<long>(marker.name.size()) - 1, marker.y - 2},
        }};
        const auto position = std::find_if(candidates.begin(), candidates.end(),
            [&](const auto& candidate) { return label_fits(candidate.first, candidate.second); });
        if (position == candidates.end()) continue;
        const auto write_line = [&](const std::string_view text, const long row) {
            for (std::size_t index = 0; index < text.size(); ++index) {
                const auto column = static_cast<std::size_t>(position->first) + index;
                const auto target_row = static_cast<std::size_t>(row);
                canvas[target_row][column] = {text[index], marker.style};
                label_cells[target_row][column] = true;
            }
        };
        write_line(marker.name, position->second);
        if (!marker.range.empty()) write_line(marker.range, position->second + 1);
    }

    std::vector<std::string> output;
    output.reserve(height_);
    auto title = std::string{"Sky view: N up E right | @ focus * above o below"};
    if (title.size() < width_) title.append(width_ - title.size(), ' ');
    else if (title.size() > width_) title.resize(width_);
    output.push_back(std::move(title));
    for (const auto& row : canvas) output.push_back("|" + styled_row(row) + "|");
    output.push_back("+" + std::string(width_ - 2, '-') + "+");
    return output;
}

}  // namespace arctra
