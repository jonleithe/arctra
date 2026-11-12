#ifndef ARCTRA_EPHEMERIS_H
#define ARCTRA_EPHEMERIS_H

#include "arctra/prediction.h"

namespace arctra {

struct EarthFixedPosition {
    double x_m;
    double y_m;
    double z_m;
};

// Supplies satellite positions in an Earth-fixed frame. The concrete CPF
// implementation will own parsing, coverage checks and interpolation policy.
class EphemerisProvider {
public:
    virtual ~EphemerisProvider() = default;

    [[nodiscard]] virtual EarthFixedPosition position_at(
        const Satellite& satellite,
        UtcTime time) const = 0;
};

}  // namespace arctra

#endif
