#include "arctra/coordinates.h"
#include "arctra/cpf.h"
#include "arctra/pass_finder.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
void require(const bool condition) {
    if (!condition) throw std::runtime_error("pass-finder assertion failed");
}
}

int main() {
    using namespace std::chrono_literals;
    const arctra::UtcTime start{};
    const arctra::GeodeticPosition station_position{0.0, 0.0, 0.0};
    const auto station_ecef = arctra::geodetic_to_ecef(station_position);

    std::vector<arctra::CpfPositionRecord> positions;
    for (int index = 0; index < 10; ++index) {
        positions.push_back({
            start + std::chrono::minutes{index}, 0,
            {station_ecef.x_m + 1'000'000.0, station_ecef.y_m, station_ecef.z_m}});
    }
    const arctra::CpfHeader header{
        2, "TEST", "lageos1", 7603901, 1155, 8820,
        start, start + 9min, 60, 0};
    const arctra::CpfEphemeris ephemeris{header, std::move(positions)};
    const arctra::Observatory observatory{"Test", station_position, 10.0};
    const arctra::Satellite satellite{"lageos1", "LAGEOS-1", 8820, 7603901};

    const auto passes = arctra::find_passes(
        ephemeris, satellite, observatory, {start, 9min});
    require(passes.size() == 1);
    require(!passes.front().aos.has_value());
    require(!passes.front().los.has_value());
    require(std::abs(passes.front().culmination.look.elevation_deg - 90.0) < 1.0e-9);
    require(std::abs(passes.front().culmination.look.slant_range_m - 1'000'000.0) < 1.0e-6);
    return EXIT_SUCCESS;
}
