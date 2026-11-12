#include "arctra/pass_finder.h"

#include "arctra/coordinates.h"

#include <chrono>
#include <optional>
#include <stdexcept>

namespace arctra {
namespace {

LookAngle look_at(const CpfEphemeris& ephemeris,
                  const Observatory& observatory,
                  const UtcTime time) {
    return topocentric_look_angle(observatory.position(), ephemeris.interpolate(time));
}

UtcTime refine_crossing(const CpfEphemeris& ephemeris,
                        const Observatory& observatory,
                        UtcTime lower,
                        UtcTime upper,
                        const bool rising) {
    const double threshold = observatory.minimum_elevation_deg();
    while (upper - lower > std::chrono::milliseconds{10}) {
        const auto middle = lower + (upper - lower) / 2;
        const bool above = look_at(ephemeris, observatory, middle).elevation_deg >= threshold;
        if (above == rising) upper = middle;
        else lower = middle;
    }
    return lower + (upper - lower) / 2;
}

PassEvent refine_peak(const CpfEphemeris& ephemeris,
                      const Observatory& observatory,
                      UtcTime lower,
                      UtcTime upper) {
    // Golden-section search is adequate because one visibility pass has one
    // dominant elevation maximum over this short bracket.
    constexpr double ratio = 0.6180339887498948482;
    while (upper - lower > std::chrono::milliseconds{20}) {
        const auto width = upper - lower;
        const auto left = lower + std::chrono::duration_cast<UtcTime::duration>(
            std::chrono::duration<double>{width} * (1.0 - ratio));
        const auto right = lower + std::chrono::duration_cast<UtcTime::duration>(
            std::chrono::duration<double>{width} * ratio);
        if (look_at(ephemeris, observatory, left).elevation_deg <
            look_at(ephemeris, observatory, right).elevation_deg) {
            lower = left;
        } else {
            upper = right;
        }
    }
    const auto time = lower + (upper - lower) / 2;
    return {time, look_at(ephemeris, observatory, time)};
}

}  // namespace

PredictionTable find_passes(const CpfEphemeris& ephemeris,
                            const Satellite& satellite,
                            const Observatory& observatory,
                            const PredictionInterval& interval) {
    if (interval.duration <= std::chrono::seconds::zero()) {
        throw std::invalid_argument("prediction duration must be positive");
    }
    const auto end = interval.start + interval.duration;
    if (interval.start < ephemeris.header().intended_start ||
        end > ephemeris.header().intended_end) {
        throw std::out_of_range("CPF does not cover the complete requested interval");
    }

    constexpr auto scan_step = std::chrono::seconds{30};
    const double threshold = observatory.minimum_elevation_deg();
    PredictionTable passes;
    auto previous_time = interval.start;
    bool previous_above = look_at(ephemeris, observatory, previous_time).elevation_deg >= threshold;
    std::optional<PassEvent> aos;
    UtcTime visible_start = interval.start;

    for (auto time = interval.start + scan_step; time <= end; time += scan_step) {
        const bool above = look_at(ephemeris, observatory, time).elevation_deg >= threshold;
        if (!previous_above && above) {
            const auto crossing = refine_crossing(ephemeris, observatory, previous_time, time, true);
            aos = PassEvent{crossing, look_at(ephemeris, observatory, crossing)};
            visible_start = crossing;
        } else if (previous_above && !above) {
            const auto crossing = refine_crossing(ephemeris, observatory, previous_time, time, false);
            const PassEvent los{crossing, look_at(ephemeris, observatory, crossing)};
            passes.push_back({satellite, aos,
                              refine_peak(ephemeris, observatory, visible_start, crossing), los});
            aos.reset();
        }
        previous_time = time;
        previous_above = above;
    }

    if (previous_time < end) {
        const bool above = look_at(ephemeris, observatory, end).elevation_deg >= threshold;
        if (!previous_above && above) {
            const auto crossing = refine_crossing(ephemeris, observatory, previous_time, end, true);
            aos = PassEvent{crossing, look_at(ephemeris, observatory, crossing)};
            visible_start = crossing;
        } else if (previous_above && !above) {
            const auto crossing = refine_crossing(ephemeris, observatory, previous_time, end, false);
            const PassEvent los{crossing, look_at(ephemeris, observatory, crossing)};
            passes.push_back({satellite, aos,
                              refine_peak(ephemeris, observatory, visible_start, crossing), los});
            aos.reset();
        }
        previous_above = above;
    }
    if (previous_above) {
        passes.push_back({satellite, aos,
                          refine_peak(ephemeris, observatory, visible_start, end), std::nullopt});
    }
    return passes;
}

}  // namespace arctra
