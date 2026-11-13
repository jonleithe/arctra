#include "arctra/satellite.h"
#include "arctra/satellite_selector.h"

#include <cstdlib>
#include <stdexcept>

namespace {

void require(const bool condition) {
    if (!condition) throw std::runtime_error("satellite-selector assertion failed");
}

}  // namespace

int main() {
    const auto catalog = arctra::SatelliteCatalog::load_default();
    arctra::SatelliteSelectionModel model{catalog};

    require(model.visible().size() == 144);
    require(model.selected_count() == 0);
    model.move_end();
    require(model.cursor() == 143);
    model.move_up();
    require(model.cursor() == 142);
    model.move_home();
    require(model.cursor() == 0);

    model.set_query("etalon");
    require(model.visible().size() == 144);
    require(model.match_count() == 2);
    require(model.visible()[model.cursor()]->key == "etalon1");
    model.toggle_current();
    model.move_down();
    model.toggle_current();
    require(model.selected_count() == 2);

    // Selection survives filtering, which is essential for incremental TUI use.
    model.set_query("lageos");
    require(model.selected_count() == 2);
    require(model.match_count() == 2);
    model.toggle_current();
    model.move_down();
    model.toggle_current();
    require(model.selected_count() == 4);

    const auto selected = model.selected_satellites();
    require(selected.size() == 4);
    require(selected[0].key == "etalon1");
    require(selected[1].key == "etalon2");
    require(selected[2].key == "lageos1");
    require(selected[3].key == "lageos2");

    model.set_query("apollo");
    require(model.match_count() == 3);
    model.toggle_current();
    require(model.selected_count() == 4);  // Catalog-only entries stay disabled.
    model.clear_selection();
    require(model.selected_count() == 0);

    model.set_query("lgs1");
    require(model.match_count() == 1);
    require(model.visible()[model.cursor()]->key == "lageos1");
    return EXIT_SUCCESS;
}
