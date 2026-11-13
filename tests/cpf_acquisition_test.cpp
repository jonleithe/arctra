#include "arctra/cpf_acquisition.h"
#include "arctra/satellite.h"

#include <cstdlib>
#include <filesystem>
#include <stdexcept>

namespace {

void require(const bool condition) {
    if (!condition) throw std::runtime_error("CPF acquisition assertion failed");
}

bool rejects(const std::filesystem::path& path, const arctra::Satellite& satellite) {
    try {
        arctra::validate_cpf_file(path, satellite);
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

}  // namespace

int main() {
    const auto fixture = std::filesystem::path{ARCTRA_TEST_FIXTURES} /
                         "lageos1_cpf_261110_excerpt.dgf";
    const auto lageos1 = *arctra::SatelliteCatalog::find("lageos1");
    const auto lageos2 = *arctra::SatelliteCatalog::find("lageos2");

    arctra::validate_cpf_file(fixture, lageos1);
    require(rejects(fixture, lageos2));
    return EXIT_SUCCESS;
}
