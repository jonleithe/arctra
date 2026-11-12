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
    const auto satellites = arctra::SatelliteCatalog::all();
    const auto lageos1 = arctra::SatelliteCatalog::find("LAGEOS-1");
    require(satellites.size() == 6);
    require(lageos1.has_value());
    require(lageos1->norad_catalog_id == 8820);
    const arctra::Observatory observatory{"Ny-Alesund example", {78.923, 11.923, 50.0}, 10.0};
    require(observatory.minimum_elevation_deg() == 10.0);
    const arctra::PredictionInterval interval{arctra::UtcTime{}, 24h};
    require(interval.duration == 24h);
    return EXIT_SUCCESS;
}
