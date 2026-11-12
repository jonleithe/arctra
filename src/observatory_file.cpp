#include "arctra/observatory_file.h"

#include <cctype>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace arctra {
namespace {
std::string trim(std::string_view value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return std::string{value};
}

double parse_number(const std::string& value, const std::string_view key,
                    const std::size_t line_number) {
    std::size_t consumed{};
    try {
        const double result = std::stod(value, &consumed);
        if (consumed != value.size()) {
            throw std::invalid_argument{"trailing characters"};
        }
        return result;
    } catch (const std::exception&) {
        throw std::runtime_error("invalid number for '" + std::string{key} +
                                 "' on line " + std::to_string(line_number));
    }
}
}  // namespace

Observatory load_observatory_ini(const std::filesystem::path& path) {
    std::ifstream input{path};
    if (!input) {
        throw std::runtime_error("cannot open observatory file: " + path.string());
    }
    std::string name;
    std::optional<double> latitude, longitude, height, minimum_elevation;
    bool in_section = false;
    std::string line;
    std::size_t line_number{};
    while (std::getline(input, line)) {
        ++line_number;
        const auto content = trim(line);
        if (content.empty() || content.front() == '#' || content.front() == ';') {
            continue;
        }
        if (content.front() == '[' && content.back() == ']') {
            in_section = trim(std::string_view{content}.substr(1, content.size() - 2)) == "observatory";
            continue;
        }
        if (!in_section) {
            continue;
        }
        const auto separator = content.find('=');
        if (separator == std::string::npos) {
            throw std::runtime_error("expected key=value on line " + std::to_string(line_number));
        }
        const auto key = trim(std::string_view{content}.substr(0, separator));
        const auto value = trim(std::string_view{content}.substr(separator + 1));
        if (key == "name") name = value;
        else if (key == "lat") latitude = parse_number(value, key, line_number);
        else if (key == "lon") longitude = parse_number(value, key, line_number);
        else if (key == "height") height = parse_number(value, key, line_number);
        else if (key == "min-elevation") minimum_elevation = parse_number(value, key, line_number);
        else throw std::runtime_error("unknown observatory key '" + key + "' on line " + std::to_string(line_number));
    }
    if (name.empty() || !latitude || !longitude || !height || !minimum_elevation) {
        throw std::runtime_error("observatory file requires name, lat, lon, height and min-elevation");
    }
    return {std::move(name), {*latitude, *longitude, *height}, *minimum_elevation};
}
}  // namespace arctra
