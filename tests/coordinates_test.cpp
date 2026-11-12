#include "arctra/coordinates.h"

#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace {
void near(const double actual, const double expected, const double tolerance) {
    if (std::abs(actual - expected) > tolerance) {
        throw std::runtime_error("coordinate assertion failed");
    }
}
}

int main() {
    const arctra::GeodeticPosition origin{0.0, 0.0, 0.0};
    const auto ecef = arctra::geodetic_to_ecef(origin);
    near(ecef.x_m, 6378137.0, 1.0e-9);
    near(ecef.y_m, 0.0, 1.0e-9);
    near(ecef.z_m, 0.0, 1.0e-9);

    const auto overhead = arctra::topocentric_look_angle(origin, {6379137.0, 0.0, 0.0});
    near(overhead.elevation_deg, 90.0, 1.0e-12);
    near(overhead.slant_range_m, 1000.0, 1.0e-9);

    const auto east = arctra::topocentric_look_angle(origin, {6378137.0, 1000.0, 0.0});
    near(east.azimuth_deg, 90.0, 1.0e-12);
    near(east.elevation_deg, 0.0, 1.0e-12);
    return EXIT_SUCCESS;
}
