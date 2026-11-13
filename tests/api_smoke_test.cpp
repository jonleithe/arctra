#include "arctra/observatory.h"
#include "arctra/prediction.h"
#include "arctra/satellite.h"

#include <chrono>
#include <cstdlib>
#include <stdexcept>

void require(const bool condition) {
    if (!condition) throw std::runtime_error("API smoke-test assertion failed");
}

int main() {
    using namespace std::chrono_literals;
    const auto catalog = arctra::SatelliteCatalog::load_default();
    const auto satellites = catalog.all();
    const auto* lageos1 = catalog.find("LAGEOS-1");
    require(satellites.size() == 144);
    require(lageos1 != nullptr);
    require(lageos1->norad_catalog_id == 8820);
    const auto* hy2b = catalog.find("HY-2B");
    require(hy2b != nullptr);
    require(hy2b->ilrs_satellite_id == 1808101);
    require(catalog.find("08820") == lageos1);
    require(catalog.find("7603901") == lageos1);
    const auto lageos_matches = catalog.search("lageos");
    require(lageos_matches.size() == 2);
    const auto* apollo11 = catalog.find("apollo 11");
    require(apollo11 != nullptr);
    require(!apollo11->is_earth_orbiting());
    const arctra::Observatory observatory{"Ny-Alesund example", {78.923, 11.923, 50.0}, 10.0};
    require(observatory.minimum_elevation_deg() == 10.0);
    const arctra::PredictionInterval interval{arctra::UtcTime{}, 24h};
    require(interval.duration == 24h);
    return EXIT_SUCCESS;
}
