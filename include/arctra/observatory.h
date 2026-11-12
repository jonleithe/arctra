#ifndef ARCTRA_OBSERVATORY_H
#define ARCTRA_OBSERVATORY_H

#include <string>

namespace arctra {

struct GeodeticPosition {
    double latitude_deg;
    double longitude_deg;
    double ellipsoidal_height_m;
};

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
