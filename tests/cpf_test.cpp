#include "arctra/cpf.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>

void require(const bool condition) {
    if (!condition) throw std::runtime_error("CPF parser assertion failed");
}

int main(const int argc, char* argv[]) {
    const auto path = argc == 2
        ? std::filesystem::path{argv[1]}
        : std::filesystem::path{ARCTRA_TEST_FIXTURES} /
              "lageos1_cpf_261110_excerpt.dgf";
    const auto cpf = arctra::load_cpf(path);
    require(cpf.header().format_version == 2);
    require(cpf.header().ephemeris_source == "DGF");
    require(cpf.header().target_name == "lageos1");
    require(cpf.header().norad_catalog_id == 8820);
    require(cpf.header().ilrs_satellite_id == 7603901);
    require(cpf.header().reference_frame == 0);
    require(cpf.positions().size() >= 10);
    require(std::abs(cpf.positions().front().position.x_m - (-9249789.876)) < 0.001);
    require(cpf.positions()[1].epoch - cpf.positions()[0].epoch == std::chrono::seconds{60});
    const auto exact = cpf.interpolate(cpf.positions()[4].epoch);
    require(exact.x_m == cpf.positions()[4].position.x_m);

    const auto midpoint = cpf.interpolate(cpf.positions()[4].epoch + std::chrono::seconds{30});
    require(std::isfinite(midpoint.x_m));
    require(std::isfinite(midpoint.y_m));
    require(std::isfinite(midpoint.z_m));
    return EXIT_SUCCESS;
}
