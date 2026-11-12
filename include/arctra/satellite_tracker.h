#ifndef ARCTRA_SATELLITE_TRACKER_H
#define ARCTRA_SATELLITE_TRACKER_H

#include "arctra/ephemeris.h"
#include "arctra/observatory.h"
#include "arctra/prediction.h"

#include <span>

namespace arctra {

// Coordinates pass searching for one observatory. It does not download or
// propagate orbits; those responsibilities belong to EphemerisProvider.
class SatelliteTracker {
public:
    SatelliteTracker(const EphemerisProvider& ephemeris, Observatory observatory);

    [[nodiscard]] PredictionTable predict(
        std::span<const Satellite> satellites,
        const PredictionInterval& interval) const;

    [[nodiscard]] const Observatory& observatory() const noexcept;

private:
    const EphemerisProvider& ephemeris_;
    Observatory observatory_;
};

}  // namespace arctra

#endif
