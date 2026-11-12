#ifndef ARCTRA_SATELLITE_H
#define ARCTRA_SATELLITE_H

#include <optional>
#include <span>
#include <string_view>

namespace arctra {

struct Satellite {
    std::string_view key;
    std::string_view name;
    int norad_catalog_id;
    int ilrs_satellite_id;
};

class SatelliteCatalog {
public:
    [[nodiscard]] static std::span<const Satellite> all() noexcept;

    // Lookup is case-insensitive and ignores '-', '_' and ASCII whitespace.
    [[nodiscard]] static std::optional<Satellite> find(std::string_view name) noexcept;
};

}  // namespace arctra

#endif
