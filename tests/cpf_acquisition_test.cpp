#include "arctra/cpf_acquisition.h"
#include "arctra/satellite.h"

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

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

void require_contains(const std::string& value, const std::string& expected) {
    require(value.find(expected) != std::string::npos);
}

}  // namespace

int main() {
    const auto fixture = std::filesystem::path{ARCTRA_TEST_FIXTURES} /
                         "lageos1_cpf_261110_excerpt.dgf";
    const auto catalog = arctra::SatelliteCatalog::load_default();
    const auto lageos1 = *catalog.find("lageos1");
    const auto lageos2 = *catalog.find("lageos2");

    arctra::validate_cpf_file(fixture, lageos1);
    require(rejects(fixture, lageos2));

    // Diagnostics at the acquisition boundary must identify the selected
    // target; a generic "no CPF" message is not actionable in a batch.
    try {
        const arctra::Satellite lunar{"apollo11", "Apollo 11", std::nullopt, 100};
        const auto path = arctra::CpfAcquisition{"unused"}.acquire(lunar);
        static_cast<void>(path);
        require(false);
    } catch (const std::invalid_argument& error) {
        require_contains(error.what(), "Apollo 11");
    }
    return EXIT_SUCCESS;
}
