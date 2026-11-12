#ifndef ARCTRA_PREDICTION_H
#define ARCTRA_PREDICTION_H

#include "arctra/satellite.h"

#include <chrono>
#include <optional>
#include <vector>

namespace arctra {

using UtcTime = std::chrono::sys_time<std::chrono::milliseconds>;

struct PredictionInterval {
    UtcTime start;
    std::chrono::seconds duration{std::chrono::hours{24}};
};

struct LookAngle {
    double azimuth_deg;
    double elevation_deg;
    double slant_range_m;
};

struct PassEvent {
    UtcTime time;
    LookAngle look;
};

struct PredictedPass {
    Satellite satellite;

    // Missing AOS/LOS indicates that the pass intersects that search boundary.
    std::optional<PassEvent> aos;
    PassEvent culmination;
    std::optional<PassEvent> los;
};

using PredictionTable = std::vector<PredictedPass>;

}  // namespace arctra

#endif
