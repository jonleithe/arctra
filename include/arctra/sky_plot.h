#ifndef ARCTRA_SKY_PLOT_H
#define ARCTRA_SKY_PLOT_H

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arctra {

// One instantaneous geometric direction to draw in the terminal sky plot.
// Azimuth is degrees clockwise from north; elevation is degrees above the
// geometric horizon. This small view model deliberately has no CPF, station,
// or terminal dependencies so the visual layer stays separate from tracking.
struct SkyPlotTarget {
    std::string_view name;
    double azimuth_deg;
    double elevation_deg;
    double slant_range_m;
    bool focused{};
    bool above_minimum_elevation{};
};

// Renders a polar sky view using ordinary terminal cells. The configured
// minimum-elevation ring is the outer boundary and the zenith is the centre;
// north is up and east is right. Targets below the threshold are shown on that
// boundary in their correct azimuthal direction. It is a qualitative display
// of instantaneous geometric directions, not a telescope-pointing display.
class SkyPlot {
public:
    SkyPlot(std::size_t width, std::size_t height, double minimum_elevation_deg);

    [[nodiscard]] static constexpr std::size_t minimum_width() noexcept { return 34; }
    [[nodiscard]] static constexpr std::size_t minimum_height() noexcept { return 7; }
    [[nodiscard]] bool drawable() const noexcept;
    [[nodiscard]] std::vector<std::string> render(std::span<const SkyPlotTarget> targets) const;

private:
    std::size_t width_;
    std::size_t height_;
    double minimum_elevation_deg_;
};

}  // namespace arctra

#endif
