#ifndef ARCTRA_EPHEMERIS_H
#define ARCTRA_EPHEMERIS_H

#include "arctra/prediction.h"

namespace arctra{

struct EarthFixedPosition
{
    // Cartesian metres in the provider's documented terrestrial frame.
    double x_m;
    double y_m;
    double z_m;
};

// Future common interface for sources such as CPF and an independently
// validated SGP4 path. Implementations own their parsing, coverage checks,
// frame treatment, and interpolation/propagation policy.
class EphemerisProvider
{
public:
    virtual ~EphemerisProvider() = default;

    [[nodiscard]] virtual EarthFixedPosition position_at(
        const Satellite& satellite,
        UtcTime time) const = 0;
};

}  // namespace arctra

#endif
