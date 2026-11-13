#ifndef ARCTRA_OBSERVATORY_H
#define ARCTRA_OBSERVATORY_H

#include <string>

namespace arctra {

struct GeodeticPosition {
    // Geodetic, rather than geocentric, latitude in decimal degrees.
    double latitude_deg;
    // Longitude east of Greenwich in decimal degrees.
    double longitude_deg;
    // Height above the WGS 84 reference ellipsoid, in metres; it is not an
    // orthometric height above the geoid.
    double ellipsoidal_height_m;
};

// Immutable observing-site configuration used by coordinate and pass-search
// routines. Frame realization and coordinate epoch remain configuration
// provenance outside this minimal data model.
class Observatory {
public:
    Observatory(std::string name,
                GeodeticPosition position,
                double minimum_elevation_deg);

    [[nodiscard]] const std::string& name() const noexcept;
    [[nodiscard]] const GeodeticPosition& position() const noexcept;
    [[nodiscard]] double minimum_elevation_deg() const noexcept;

private:
    std::string name_;
    GeodeticPosition position_;
    double minimum_elevation_deg_;
};

}  // namespace arctra

#endif
