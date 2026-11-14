#ifndef ARCTRA_SATELLITE_SELECTOR_H
#define ARCTRA_SATELLITE_SELECTOR_H

#include "arctra/satellite.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace arctra {

// UI-independent selection state. A future richer TUI can replace the terminal
// view without changing filtering, cursor, or persistent multi-selection rules.
class SatelliteSelectionModel {
public:
    explicit SatelliteSelectionModel(const SatelliteCatalog& catalog);

    void set_query(std::string query);
    void clear_query() noexcept;
    [[nodiscard]] const std::string& query() const noexcept;
    [[nodiscard]] std::size_t match_count() const noexcept;
    [[nodiscard]] const std::vector<const Satellite*>& visible() const noexcept;

    void move_up();
    void move_down();
    void move_home() noexcept;
    void move_end() noexcept;
    void page_up(std::size_t page_size);
    void page_down(std::size_t page_size);
    [[nodiscard]] std::size_t cursor() const noexcept;

    void toggle_current();
    // Adds one known Earth-orbiting target without changing the cursor or
    // query. Used when a caller has already acquired its CPF before opening a
    // shared selection-and-tracking view.
    void select_key(std::string_view key);
    void select_all_visible();
    void clear_selection() noexcept;
    [[nodiscard]] bool is_selected(std::string_view key) const;
    [[nodiscard]] std::size_t selected_count() const noexcept;
    [[nodiscard]] std::vector<const Satellite*> selected() const;
    [[nodiscard]] std::vector<Satellite> selected_satellites() const;

private:
    void move_by(std::ptrdiff_t amount);

    const SatelliteCatalog& catalog_;
    std::string query_;
    std::vector<const Satellite*> visible_;
    std::size_t match_count_{};
    std::unordered_set<std::string> selected_keys_;
    std::size_t cursor_{};
};

// Run the Linux terminal selector. A null result means the user cancelled.
[[nodiscard]] std::optional<std::vector<Satellite>> select_satellites_in_terminal(
    const SatelliteCatalog& catalog);

}  // namespace arctra

#endif
