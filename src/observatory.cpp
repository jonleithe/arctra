#include "arctra/observatory.h"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace arctra {

Observatory::Observatory(std::string name,
                         const GeodeticPosition position,
                         const double minimum_elevation_deg)
    : name_(std::move(name)),
      position_(position),
      minimum_elevation_deg_(minimum_elevation_deg) {
    if (name_.empty()) {
        throw std::invalid_argument("observatory name must not be empty");
    }
    if (!std::isfinite(position_.latitude_deg) ||
        position_.latitude_deg < -90.0 || position_.latitude_deg > 90.0) {
        throw std::invalid_argument("observatory latitude must be in [-90, 90] degrees");
    }
    if (!std::isfinite(position_.longitude_deg) ||
        position_.longitude_deg < -180.0 || position_.longitude_deg > 180.0) {
        throw std::invalid_argument("observatory longitude must be in [-180, 180] degrees");
    }
    if (!std::isfinite(position_.ellipsoidal_height_m)) {
        throw std::invalid_argument("observatory height must be finite");
    }
    if (!std::isfinite(minimum_elevation_deg_) ||
        minimum_elevation_deg_ < -90.0 || minimum_elevation_deg_ > 90.0) {
        throw std::invalid_argument("minimum elevation must be in [-90, 90] degrees");
    }
}

const std::string& Observatory::name() const noexcept { return name_; }

const GeodeticPosition& Observatory::position() const noexcept { return position_; }

double Observatory::minimum_elevation_deg() const noexcept {
    return minimum_elevation_deg_;
}

}  // namespace arctra
