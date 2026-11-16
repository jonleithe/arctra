#include "arctra/globe_launcher.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unistd.h>

namespace arctra {

int run_globe() {
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
    ::execlp("python3", "python3", script.c_str(), "--worker", worker.c_str(),
             "--web-root", web_root.c_str(), static_cast<char*>(nullptr));
    throw std::runtime_error("cannot start Python globe host: " +
                             std::string{std::strerror(errno)});
}

}  // namespace arctra
