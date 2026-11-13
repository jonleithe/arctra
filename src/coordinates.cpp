#include "arctra/coordinates.h"

#include <cmath>

namespace arctra{ 

namespace{    
// Keep angle conversion explicit at the boundary: public configuration and
// output use degrees, while the trigonometric functions use radians.
constexpr double pi = 3.141592653589793238462643383279502884;
constexpr double degrees_to_radians = pi / 180.0;
constexpr double radians_to_degrees = 180.0 / pi;
}



EarthFixedPosition geodetic_to_ecef(const GeodeticPosition& position)
{
    // WGS 84 defining constants. The semi-minor axis is derived so the same
    // flattening is used consistently in all three coordinate components.
    constexpr double semi_major_axis_m      = 6378137.0;
    constexpr double inverse_flattening     = 298.257223563;
    constexpr double flattening             = 1.0 / inverse_flattening;
    constexpr double eccentricity_squared   = flattening * (2.0 - flattening);

    const double latitude       = position.latitude_deg * degrees_to_radians;
    const double longitude      = position.longitude_deg * degrees_to_radians;
    const double sin_latitude   = std::sin(latitude);
    const double cos_latitude   = std::cos(latitude);

    // N is the radius of curvature of the prime vertical at this geodetic
    // latitude. It differs from both the equatorial and polar radii.
    const double prime_vertical_radius =    semi_major_axis_m /
                                            std::sqrt(1.0 - eccentricity_squared * sin_latitude * sin_latitude);

    return {
        (prime_vertical_radius + position.ellipsoidal_height_m) * cos_latitude * std::cos(longitude),
        (prime_vertical_radius + position.ellipsoidal_height_m) * cos_latitude * std::sin(longitude),
        (prime_vertical_radius * (1.0 - eccentricity_squared) +
         position.ellipsoidal_height_m) * sin_latitude,
    };
}

LookAngle topocentric_look_angle(const GeodeticPosition& observer,
                                 const EarthFixedPosition& satellite) {
    const auto station = geodetic_to_ecef(observer);
    // rho points from the station to the satellite. It must be formed only
    // after both positions have been expressed in the same terrestrial frame.
    const double dx = satellite.x_m - station.x_m;
    const double dy = satellite.y_m - station.y_m;
    const double dz = satellite.z_m - station.z_m;
    const double latitude = observer.latitude_deg * degrees_to_radians;
    const double longitude = observer.longitude_deg * degrees_to_radians;

    // ECEF-to-ENU rotation for geodetic latitude and east-positive longitude.
    // Using geocentric latitude here would tilt the local horizon.
    const double east = -std::sin(longitude) * dx + std::cos(longitude) * dy;
    const double north = -std::sin(latitude) * std::cos(longitude) * dx -
                         std::sin(latitude) * std::sin(longitude) * dy +
                         std::cos(latitude) * dz;
    const double up = std::cos(latitude) * std::cos(longitude) * dx +
                      std::cos(latitude) * std::sin(longitude) * dy +
                      std::sin(latitude) * dz;
    const double horizontal = std::hypot(east, north);
    // atan2(E, N) makes north zero and east 90 degrees. atan2 normally returns
    // [-180, 180], so normalize it to the conventional azimuth interval.
    double azimuth = std::atan2(east, north) * radians_to_degrees;
    if (azimuth < 0.0) azimuth += 360.0;

    return {
        azimuth,
        std::atan2(up, horizontal) * radians_to_degrees,
        std::hypot(horizontal, up),
    };
}

}  // namespace arctra
