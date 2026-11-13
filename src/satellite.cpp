#include "arctra/satellite.h"

#include <array>
#include <cctype>
#include <string>

namespace arctra {
namespace {

constexpr std::array<Satellite, 11> satellites{{
    // The catalog is data, not an algorithmic restriction. Add future targets
    // here (or load configuration) without changing CPF or geometry code.
    {"lageos1", "LAGEOS-1", 8820, 7603901},
    {"lageos2", "LAGEOS-2", 22195, 9207002},
    {"etalon1", "ETALON-1", 19751, 8900103},
    {"etalon2", "ETALON-2", 20026, 8903903},
    {"hy2b", "HY-2B", 43655, 1808101},
    {"hy2c", "HY-2C", 46469, 2006601},
    {"hy2d", "HY-2D", 48621, 2104301},
    {"hy2e", "HY-2E", 69737, 2614901},
    {"lares", "LARES", 38077, 1200601},
    {"lares2", "LARES-2", 53105, 2208001},
    {"stella", "STELLA", 22824, 9306102},
}};

std::string normalized(std::string_view value) {
    // Accept conventional spellings such as LAGEOS-1, lageos_1, and lageos1
    // while retaining a simple stable key for CPF filename matching.
    std::string result;
    result.reserve(value.size());
    for (const char raw_character : value) {
        const auto character = static_cast<unsigned char>(raw_character);
        if (character == '-' || character == '_' || std::isspace(character) != 0) {
            continue;
        }
        result.push_back(static_cast<char>(std::tolower(character)));
    }
    return result;
}

}  // namespace

std::span<const Satellite> SatelliteCatalog::all() noexcept { return satellites; }

std::optional<Satellite> SatelliteCatalog::find(const std::string_view name) noexcept {
    const auto key = normalized(name);
    for (const auto& satellite : satellites) {
        if (key == satellite.key || key == normalized(satellite.name)) {
            return satellite;
        }
    }
    return std::nullopt;
}

}  // namespace arctra
