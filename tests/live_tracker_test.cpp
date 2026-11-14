#include "arctra/live_tracker.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <stdexcept>
#include <vector>

namespace {

void require(const bool condition) {
    if (!condition) throw std::runtime_error("live-tracker assertion failed");
}

}  // namespace

int main() {
    using namespace std::chrono_literals;
    const auto epoch = arctra::UtcTime{std::chrono::sys_days{2026y / std::chrono::November / 14} +
                                       12h};
    arctra::CpfHeader header{2, "TST", "test", 1, 1, 1,
                             epoch - 10min, epoch + 10min, 60, 0};
    std::vector<arctra::CpfPositionRecord> positions;
    for (int minute = -5; minute <= 4; ++minute) {
        positions.push_back({epoch + std::chrono::minutes{minute}, 0,
                             {6'379'137.0, 0.0, 0.0}});
    }
    const arctra::CpfEphemeris ephemeris{header, positions};
    const arctra::Observatory observatory{"test", {0.0, 0.0, 0.0}, 10.0};
    const arctra::Satellite satellite{"test", "Test", 1, 1};

    const auto sample = arctra::evaluate_live_track(ephemeris, satellite, observatory, epoch);
    require(sample.above_minimum_elevation);
    require(std::abs(sample.look.elevation_deg - 90.0) < 1e-10);
    require(std::abs(sample.look.slant_range_m - 1'000.0) < 1e-8);
    require(std::abs(sample.one_way_light_time_us - 3.33564095198) < 1e-8);
    require(std::abs(sample.round_trip_light_time_us - 2.0 * sample.one_way_light_time_us) < 1e-10);

    // A low artificial orbit moves from below the horizon, through zenith,
    // and below again. This exercises the same refined crossing finder used
    // by the live countdown without depending on a network CPF.
    std::vector<arctra::CpfPositionRecord> moving_positions;
    constexpr double pi = 3.14159265358979323846;
    for (int minute = -5; minute <= 4; ++minute) {
        const auto angle = static_cast<double>(minute) * 0.4 * pi / 180.0;
        moving_positions.push_back({epoch + std::chrono::minutes{minute}, 0,
                                    {6'379'137.0 * std::cos(angle),
                                     6'379'137.0 * std::sin(angle), 0.0}});
    }
    auto moving_header = header;
    moving_header.intended_start = epoch - 5min;
    moving_header.intended_end = epoch + 4min;
    const arctra::CpfEphemeris moving_ephemeris{moving_header, moving_positions};
    const auto set = arctra::next_visibility_event(moving_ephemeris, satellite,
                                                    observatory, epoch);
    require(set && set->kind == arctra::VisibilityEventKind::sets_below_threshold);
    require(set->time > epoch);
    const auto rise = arctra::next_visibility_event(moving_ephemeris, satellite,
                                                     observatory, epoch - 4min);
    require(rise && rise->kind == arctra::VisibilityEventKind::rises_above_threshold);
    require(rise->time > epoch - 4min && rise->time < epoch);
    return EXIT_SUCCESS;
}
