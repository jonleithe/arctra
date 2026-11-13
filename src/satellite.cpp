#include "arctra/satellite.h"

#include <cctype>
#include <charconv>
#include <fstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace arctra {
namespace {

std::string normalized(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const char raw_character : value) {
        const auto character = static_cast<unsigned char>(raw_character);
        if (character == '-' || character == '_' || std::isspace(character) != 0) continue;
        result.push_back(static_cast<char>(std::tolower(character)));
    }
    return result;
}

std::optional<int> decimal_id(std::string_view text) {
    if (text.empty()) return std::nullopt;
    int value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value < 0) return std::nullopt;
    return value;
}

std::vector<std::string_view> fields(std::string_view line) {
    std::vector<std::string_view> result;
    while (true) {
        const auto comma = line.find(',');
        result.push_back(line.substr(0, comma));
        if (comma == std::string_view::npos) break;
        line.remove_prefix(comma + 1);
    }
    return result;
}

std::runtime_error catalog_error(const std::filesystem::path& path, const std::size_t line,
                                 const std::string& message) {
    return std::runtime_error("invalid satellite catalog " + path.string() + " at line " +
                              std::to_string(line) + ": " + message);
}

bool id_matches(const Satellite& satellite, const int value) {
    return satellite.ilrs_satellite_id == value ||
           (satellite.norad_catalog_id && *satellite.norad_catalog_id == value);
}

bool is_subsequence(const std::string_view needle, const std::string_view value) {
    std::size_t matched{};
    for (const char character : value) {
        if (matched < needle.size() && needle[matched] == character) ++matched;
    }
    return matched == needle.size();
}

}  // namespace

bool Satellite::is_earth_orbiting() const noexcept { return norad_catalog_id.has_value(); }

SatelliteCatalog::SatelliteCatalog(std::vector<Satellite> satellites)
    : satellites_(std::move(satellites)) {}

SatelliteCatalog SatelliteCatalog::load_default() {
#ifdef ARCTRA_DEFAULT_CATALOG_PATH
    return load(ARCTRA_DEFAULT_CATALOG_PATH);
#else
    return load("data/edc-active-satellites.csv");
#endif
}

SatelliteCatalog SatelliteCatalog::load(const std::filesystem::path& path) {
    std::ifstream input{path};
    if (!input) throw std::runtime_error("cannot open satellite catalog: " + path.string());

    std::vector<Satellite> satellites;
    std::unordered_set<std::string> keys;
    std::string line;
    std::size_t line_number{};
    while (std::getline(input, line)) {
        ++line_number;
        if (line_number == 1 && line == "cpf_key,name,ilrs_id,norad_id") continue;
        if (line.empty() || line.front() == '#') continue;
        const auto columns = fields(line);
        if (columns.size() != 4) throw catalog_error(path, line_number, "expected four CSV fields");

        Satellite satellite{std::string{columns[0]}, std::string{columns[1]}, std::nullopt, 0};
        if (satellite.key.empty() || satellite.name.empty()) {
            throw catalog_error(path, line_number, "key and name must not be empty");
        }
        const auto ilrs = decimal_id(columns[2]);
        if (!ilrs) throw catalog_error(path, line_number, "invalid ILRS ID");
        satellite.ilrs_satellite_id = *ilrs;
        if (!columns[3].empty()) {
            satellite.norad_catalog_id = decimal_id(columns[3]);
            if (!satellite.norad_catalog_id) throw catalog_error(path, line_number, "invalid NORAD ID");
        }
        const auto canonical_key = normalized(satellite.key);
        if (canonical_key != satellite.key) {
            throw catalog_error(path, line_number, "CPF key must already be normalized");
        }
        if (!keys.insert(canonical_key).second) {
            throw catalog_error(path, line_number, "duplicate CPF key " + satellite.key);
        }
        satellites.push_back(std::move(satellite));
    }
    if (satellites.empty()) throw std::runtime_error("satellite catalog is empty: " + path.string());
    return SatelliteCatalog{std::move(satellites)};
}

std::span<const Satellite> SatelliteCatalog::all() const noexcept { return satellites_; }

const Satellite* SatelliteCatalog::find(const std::string_view value) const noexcept {
    const auto query = normalized(value);
    const auto numeric = decimal_id(query);
    for (const auto& satellite : satellites_) {
        if (query == satellite.key || query == normalized(satellite.name) ||
            (numeric && id_matches(satellite, *numeric))) return &satellite;
    }
    return nullptr;
}

std::vector<const Satellite*> SatelliteCatalog::search(const std::string_view query) const {
    const auto needle = normalized(query);
    const auto numeric_query = decimal_id(needle);
    const auto id_needle = numeric_query ? std::to_string(*numeric_query) : needle;
    std::vector<const Satellite*> matches;
    std::vector<const Satellite*> fuzzy_matches;
    for (const auto& satellite : satellites_) {
        const auto name = normalized(satellite.name);
        const auto ilrs = std::to_string(satellite.ilrs_satellite_id);
        const auto norad = satellite.norad_catalog_id
                               ? std::to_string(*satellite.norad_catalog_id)
                               : std::string{};
        if (needle.empty() || satellite.key.find(needle) != std::string::npos ||
            name.find(needle) != std::string::npos || ilrs.find(id_needle) != std::string::npos ||
            (!norad.empty() && norad.find(id_needle) != std::string::npos)) {
            matches.push_back(&satellite);
        } else if (is_subsequence(needle, satellite.key) || is_subsequence(needle, name)) {
            fuzzy_matches.push_back(&satellite);
        }
    }
    // Prefer predictable substring results. Fuzzy subsequence matching is a
    // fallback for compact queries such as "lgs1" -> "lageos1".
    return matches.empty() ? fuzzy_matches : matches;
}

}  // namespace arctra
