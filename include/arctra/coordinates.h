#ifndef ARCTRA_COORDINATES_H
#define ARCTRA_COORDINATES_H

#include "arctra/ephemeris.h"
#include "arctra/observatory.h"
#include "arctra/prediction.h"

namespace arctra {

// Converts geodetic coordinates using the WGS 84 reference ellipsoid.
[[nodiscard]] EarthFixedPosition geodetic_to_ecef(const GeodeticPosition& position);

// Both positions must be expressed in the same Earth-fixed reference frame.
[[nodiscard]] LookAngle topocentric_look_angle(
    const GeodeticPosition& observer,
    const EarthFixedPosition& satellite);

}  // namespace arctra

#endif
