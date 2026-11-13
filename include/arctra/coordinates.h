#ifndef ARCTRA_COORDINATES_H
#define ARCTRA_COORDINATES_H

#include "arctra/ephemeris.h"
#include "arctra/observatory.h"
#include "arctra/prediction.h"

namespace arctra {

// Converts latitude, longitude, and ellipsoidal height to Cartesian ECEF
// metres using the WGS 84 reference ellipsoid.
[[nodiscard]] EarthFixedPosition geodetic_to_ecef(const GeodeticPosition& position);

// Forms satellite minus observer in ECEF, rotates that vector into the local
// east-north-up frame, and derives geometric look angles. Both positions must
// refer to the same Earth-fixed reference frame and coordinate epoch.
[[nodiscard]] LookAngle topocentric_look_angle(
    const GeodeticPosition& observer,
    const EarthFixedPosition& satellite);

}  // namespace arctra

#endif
