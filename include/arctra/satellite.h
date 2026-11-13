#ifndef ARCTRA_SATELLITE_H
#define ARCTRA_SATELLITE_H

#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace arctra {

struct Satellite {
    std::string key;
    std::string name;
    std::optional<int> norad_catalog_id;
    int ilrs_satellite_id{};

    [[nodiscard]] bool is_earth_orbiting() const noexcept;
};

class SatelliteCatalog {
public:
    [[nodiscard]] static SatelliteCatalog load_default();
    [[nodiscard]] static SatelliteCatalog load(const std::filesystem::path& path);

    [[nodiscard]] std::span<const Satellite> all() const noexcept;
    [[nodiscard]] const Satellite* find(std::string_view value) const noexcept;
    [[nodiscard]] std::vector<const Satellite*> search(std::string_view query) const;

private:
    explicit SatelliteCatalog(std::vector<Satellite> satellites);
    std::vector<Satellite> satellites_;
};

}  // namespace arctra

#endif
