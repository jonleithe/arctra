#include "arctra/globe_launcher.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

namespace arctra {

int run_globe(const std::optional<Observatory>& observatory,
              const std::vector<Satellite>& initial_satellites,
              const bool override_saved_targets) {
    std::array<char, 4096> executable_buffer{};
    const auto length = ::readlink("/proc/self/exe", executable_buffer.data(),
                                   executable_buffer.size() - 1);
    if (length < 0) {
        throw std::runtime_error("cannot locate the Arctra executable: " +
                                 std::string{std::strerror(errno)});
    }
    const auto executable = std::filesystem::path{
        std::string{executable_buffer.data(), static_cast<std::size_t>(length)}};
    const auto worker = executable.parent_path() / "arctra-globe-worker";
    const auto script = std::filesystem::path{ARCTRA_SOURCE_DIR} / "scripts/globe_server.py";
    const auto web_root = std::filesystem::path{ARCTRA_SOURCE_DIR} / "globe/dist";
    if (!std::filesystem::exists(worker)) {
        throw std::runtime_error("globe worker is missing; rebuild Arctra");
    }
    if (!std::filesystem::exists(web_root / "index.html")) {
        throw std::runtime_error(
            "globe web assets are missing; run ./scripts/build-globe.sh debug");
    }
    std::vector<std::string> arguments{"python3", script.string(), "--worker", worker.string(),
                                       "--web-root", web_root.string()};
    if (observatory) {
        arguments.insert(arguments.end(), {
            "--observer-name", observatory->name(),
            "--observer-lat", std::to_string(observatory->position().latitude_deg),
            "--observer-lon", std::to_string(observatory->position().longitude_deg),
            "--observer-height", std::to_string(observatory->position().ellipsoidal_height_m),
            "--observer-elevation", std::to_string(observatory->minimum_elevation_deg())});
    }
    if (override_saved_targets) arguments.push_back("--override-targets");
    for (const auto& satellite : initial_satellites) {
        arguments.insert(arguments.end(), {"--initial-target", satellite.key});
    }
    std::vector<char*> argument_pointers;
    argument_pointers.reserve(arguments.size() + 1);
    for (auto& argument : arguments) argument_pointers.push_back(argument.data());
    argument_pointers.push_back(nullptr);
    ::execvp("python3", argument_pointers.data());
    throw std::runtime_error("cannot start Python globe host: " +
                             std::string{std::strerror(errno)});
}

}  // namespace arctra
