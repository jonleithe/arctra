#ifndef ARCTRA_PREDICTION_H
#define ARCTRA_PREDICTION_H

#include "arctra/satellite.h"

#include <chrono>
#include <optional>
#include <vector>

namespace arctra
{
// CPF epochs are labelled UTC and are stored with millisecond resolution. A
// sys_time cannot represent the UTC 23:59:60 label distinctly; consequently
// this educational implementation must not be used to predict across a leap
// second without a dedicated UTC time-scale implementation.
using UtcTime = std::chrono::sys_time<std::chrono::milliseconds>;

// The time span for which passes are requested. The caller is responsible for
// obtaining an ephemeris that covers the complete closed interval.
struct PredictionInterval {
    UtcTime start;
    std::chrono::seconds duration{std::chrono::hours{24}};
};

// Instantaneous geometric line of sight in the observer's local ENU frame.
// Angles are degrees; azimuth is clockwise from north in [0, 360), elevation
// is positive above the local horizon, and range is metres.
struct LookAngle {
    double azimuth_deg;
    double elevation_deg;
    double slant_range_m;
};

// One event on a predicted pass, retaining the full look angle so a later CLI
// or tracking interface need not recompute it.
struct PassEvent {
    UtcTime time;
    LookAngle look;
};

struct PredictedPass {
    Satellite satellite;

    // A missing AOS or LOS means visibility began before or continues beyond
    // the requested interval respectively; it is not an unreported crossing.
    std::optional<PassEvent> aos;
    PassEvent culmination;
    std::optional<PassEvent> los;
};

using PredictionTable = std::vector<PredictedPass>;

}  // namespace arctra

#endif
