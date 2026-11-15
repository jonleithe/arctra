#include "arctra/sky_plot.h"

#include <cstdlib>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(const bool condition) {
    if (!condition) throw std::runtime_error("sky-plot assertion failed");
}

std::string without_sgr(const std::string_view text) {
    std::string result;
    for (std::size_t index = 0; index < text.size();) {
        if (text[index] == '\x1b' && index + 1 < text.size() && text[index + 1] == '[') {
            const auto end = text.find('m', index + 2);
            if (end != std::string_view::npos) {
                index = end + 1;
                continue;
            }
        }
        result += text[index++];
    }
    return result;
}

}  // namespace

int main() {
    arctra::SkyPlot narrow{arctra::SkyPlot::minimum_width() - 1,
                           arctra::SkyPlot::minimum_height(), 10.0};
    require(!narrow.drawable());
    require(narrow.render({}).empty());

    arctra::SkyPlot plot{40, 9, 10.0};
    require(plot.drawable());
    const std::vector<arctra::SkyPlotTarget> targets{
        {"focus", 0.0, 90.0, 1'000'000.0, true, true},
        {"east", 90.0, 45.0, 2'000'000.0, false, true},
        {"west", 270.0, 0.0, 3'000'000.0, false, false},
    };
    const auto view = plot.render(targets);
    require(view.size() == 9);
    for (const auto& line : view) require(without_sgr(line).size() == 40);
    require(view.front().find("@ focus") != std::string::npos);
    std::string all_lines;
    for (const auto& line : view) all_lines += line;
    require(all_lines.find("\x1b[93m") != std::string::npos);
    require(all_lines.find("\x1b[32m") != std::string::npos);
    require(all_lines.find("\x1b[90m") != std::string::npos);

    bool has_focus = false;
    bool has_above = false;
    bool has_below = false;
    bool has_focus_name = false;
    bool has_above_name = false;
    bool has_below_name = false;
    bool has_visible_range = false;
    for (auto line = std::next(view.begin()); line != view.end(); ++line) {
        const auto plain = without_sgr(*line);
        has_focus = has_focus || plain.find('@') != std::string::npos;
        has_above = has_above || plain.find('*') != std::string::npos;
        has_below = has_below || plain.find('o') != std::string::npos;
        has_focus_name = has_focus_name || plain.find("focus") != std::string::npos;
        has_above_name = has_above_name || plain.find("east") != std::string::npos;
        has_below_name = has_below_name || plain.find("west") != std::string::npos;
        has_visible_range = has_visible_range || plain.find("2 000 km") != std::string::npos;
    }
    require(has_focus && has_above && has_below && has_focus_name && has_above_name && has_below_name &&
            has_visible_range);
    return EXIT_SUCCESS;
}
