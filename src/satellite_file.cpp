#include "arctra/satellite_file.h"

#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace arctra {
namespace {

std::string_view trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return value;
}

}  // namespace

void append_satellites_from_file(std::vector<Satellite>& result,
                                 const SatelliteCatalog& catalog,
                                 const std::filesystem::path& path) {
    std::ifstream input{path};
    if (!input) {
        throw std::runtime_error("cannot open satellite file: " + path.string());
    }

    std::string line;
    std::size_t line_number{};
    while (std::getline(input, line)) {
        ++line_number;
        const auto name = trim(line);
        if (name.empty() || name.front() == '#') {
            continue;
        }

        const auto* satellite = catalog.find(name);
        if (satellite == nullptr) {
            throw std::runtime_error("unknown satellite '" + std::string{name} +
                                     "' in " + path.string() + " on line " +
                                     std::to_string(line_number));
        }
        result.push_back(*satellite);
    }
}

}  // namespace arctra
