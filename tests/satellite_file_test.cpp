#include "arctra/satellite_file.h"

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace {

void require(const bool condition) {
    if (!condition) throw std::runtime_error("satellite-file assertion failed");
}

}  // namespace

int main() {
    std::vector<arctra::Satellite> satellites;
    const auto catalog = arctra::SatelliteCatalog::load_default();
    arctra::append_satellites_from_file(
        satellites, catalog,
        std::filesystem::path{ARCTRA_TEST_FIXTURES} / "satellite_selection.txt");

    require(satellites.size() == 3);
    require(satellites[0].key == "lageos1");
    require(satellites[1].key == "lares2");
    require(satellites[2].key == "etalon1");
    return EXIT_SUCCESS;
}
