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

    // Jason-3 regression: header intent outlasts the position records.
    // A valid next crossing must survive that missing future coverage.
    moving_header.intended_end = epoch + 28min;
    const arctra::CpfEphemeris short_coverage{moving_header, moving_positions};
    const auto bounded_set = arctra::next_visibility_event(
        short_coverage, satellite, observatory, epoch);
    require(bounded_set && bounded_set->time == set->time);
    require(!arctra::next_visibility_event(ephemeris, satellite, observatory, epoch));

    // A later gap must not invalidate a crossing already found.
    for (int minute = 5; minute <= 15; ++minute) {
        const auto angle = static_cast<double>(minute) * 0.4 * pi / 180.0;
        moving_positions.push_back({epoch + std::chrono::minutes{minute}, 0,
                                    {6'379'137.0 * std::cos(angle),
                                     6'379'137.0 * std::sin(angle), 0.0}});
    }
    moving_positions.push_back({epoch + 30min, 0, moving_positions.back().position});
    moving_header.intended_end = epoch + 30min;
    const arctra::CpfEphemeris later_gap{moving_header, moving_positions};
    const auto early_set = arctra::next_visibility_event(
        later_gap, satellite, observatory, epoch);
    require(early_set && early_set->time == set->time);

    // The last partial scan step must still detect a set.
    auto partial_header = header;
    partial_header.intended_end = set->time + 1s;
    // Use moving states with the same shortened header.
    const arctra::CpfEphemeris partial_moving{partial_header,
                                            moving_ephemeris.positions()};
    const auto partial_set = arctra::next_visibility_event(
        partial_moving, satellite, observatory, set->time - 5s);
    require(partial_set && partial_set->kind == arctra::VisibilityEventKind::sets_below_threshold);
    require(std::abs(std::chrono::duration<double>(partial_set->time - set->time).count()) < 0.02);

    const arctra::Observatory opposite_station{"opposite", {0.0, 180.0, 0.0}, 10.0};
    require(!arctra::next_visibility_event(ephemeris, satellite, opposite_station, epoch));
    bool rejected_outside_coverage = false;
    try {
        (void)arctra::next_visibility_event(ephemeris, satellite, observatory, epoch + 5min);
    } catch (const std::out_of_range&) {
        rejected_outside_coverage = true;
    }
    require(rejected_outside_coverage);
    return EXIT_SUCCESS;
}
