#include "arctra/satellite_selector.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>
#include <utility>

namespace arctra {

SatelliteSelectionModel::SatelliteSelectionModel(const SatelliteCatalog& catalog)
    : catalog_(catalog), visible_(catalog.search("")), match_count_(visible_.size()) {}

void SatelliteSelectionModel::set_query(std::string query) {
    query_ = std::move(query);
    if (query_.empty()) {
        match_count_ = visible_.size();
        return;
    }
    const auto matches = catalog_.search(query_);
    match_count_ = matches.size();
    if (matches.empty()) return;
    const auto match = std::find(visible_.begin(), visible_.end(), matches.front());
    if (match != visible_.end()) {
        cursor_ = static_cast<std::size_t>(std::distance(visible_.begin(), match));
    }
}

void SatelliteSelectionModel::clear_query() noexcept {
    query_.clear();
    match_count_ = visible_.size();
}

const std::string& SatelliteSelectionModel::query() const noexcept { return query_; }
std::size_t SatelliteSelectionModel::match_count() const noexcept { return match_count_; }

const std::vector<const Satellite*>& SatelliteSelectionModel::visible() const noexcept {
    return visible_;
}

void SatelliteSelectionModel::move_by(const std::ptrdiff_t amount) {
    if (visible_.empty()) {
        cursor_ = 0;
        return;
    }
    const auto last = static_cast<std::ptrdiff_t>(visible_.size() - 1);
    const auto current = static_cast<std::ptrdiff_t>(cursor_);
    cursor_ = static_cast<std::size_t>(std::clamp(current + amount, std::ptrdiff_t{0}, last));
}

void SatelliteSelectionModel::move_up() { move_by(-1); }
void SatelliteSelectionModel::move_down() { move_by(1); }
void SatelliteSelectionModel::move_home() noexcept { cursor_ = 0; }
void SatelliteSelectionModel::move_end() noexcept {
    cursor_ = visible_.empty() ? 0 : visible_.size() - 1;
}
void SatelliteSelectionModel::page_up(const std::size_t page_size) {
    move_by(-static_cast<std::ptrdiff_t>(page_size));
}
void SatelliteSelectionModel::page_down(const std::size_t page_size) {
    move_by(static_cast<std::ptrdiff_t>(page_size));
}
std::size_t SatelliteSelectionModel::cursor() const noexcept { return cursor_; }

void SatelliteSelectionModel::toggle_current() {
    if (visible_.empty()) return;
    const auto& satellite = *visible_[cursor_];
    if (!satellite.is_earth_orbiting()) return;
    if (selected_keys_.erase(satellite.key) == 0) selected_keys_.insert(satellite.key);
}

void SatelliteSelectionModel::select_all_visible() {
    for (const auto* satellite : visible_) {
        if (satellite->is_earth_orbiting()) selected_keys_.insert(satellite->key);
    }
}

void SatelliteSelectionModel::clear_selection() noexcept { selected_keys_.clear(); }

bool SatelliteSelectionModel::is_selected(const std::string_view key) const {
    return selected_keys_.contains(std::string{key});
}

std::size_t SatelliteSelectionModel::selected_count() const noexcept {
    return selected_keys_.size();
}

std::vector<const Satellite*> SatelliteSelectionModel::selected() const {
    std::vector<const Satellite*> result;
    result.reserve(selected_keys_.size());
    for (const auto& satellite : catalog_.all()) {
        if (selected_keys_.contains(satellite.key)) result.push_back(&satellite);
    }
    return result;
}

std::vector<Satellite> SatelliteSelectionModel::selected_satellites() const {
    std::vector<Satellite> result;
    result.reserve(selected_keys_.size());
    for (const auto* satellite : selected()) result.push_back(*satellite);
    return result;
}

namespace {

class RawTerminal {
public:
    RawTerminal() {
        if (::isatty(STDIN_FILENO) == 0 || ::isatty(STDOUT_FILENO) == 0) {
            throw std::invalid_argument("--select requires an interactive terminal");
        }
        if (::tcgetattr(STDIN_FILENO, &original_) != 0) {
            throw std::runtime_error("cannot read terminal settings: " +
                                     std::string{std::strerror(errno)});
        }
        auto raw = original_;
        raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO | ISIG));
        raw.c_iflag &= static_cast<tcflag_t>(~(IXON | ICRNL));
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        if (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
            throw std::runtime_error("cannot enable terminal selection mode: " +
                                     std::string{std::strerror(errno)});
        }
        active_ = true;
        std::cout << "\x1b[?1049h\x1b[?25l" << std::flush;
    }

    RawTerminal(const RawTerminal&) = delete;
    RawTerminal& operator=(const RawTerminal&) = delete;

    ~RawTerminal() {
        if (!active_) return;
        ::tcsetattr(STDIN_FILENO, TCSAFLUSH, &original_);
        std::cout << "\x1b[?25h\x1b[?1049l" << std::flush;
    }

private:
    termios original_{};
    bool active_{};
};

enum class Key { character, up, down, home, end, page_up, page_down, enter, backspace, escape };

struct Input {
    Key key{Key::character};
    char character{};
};

bool input_available(const int timeout_ms) {
    pollfd descriptor{STDIN_FILENO, POLLIN, 0};
    return ::poll(&descriptor, 1, timeout_ms) > 0 && (descriptor.revents & POLLIN) != 0;
}

char read_byte() {
    char value{};
    if (::read(STDIN_FILENO, &value, 1) != 1) throw std::runtime_error("terminal input failed");
    return value;
}

Input read_input() {
    const char first = read_byte();
    if (first == '\r' || first == '\n') return {Key::enter, {}};
    if (first == 127 || first == '\b') return {Key::backspace, {}};
    if (first != 27) return {Key::character, first};
    if (!input_available(30)) return {Key::escape, {}};
    if (read_byte() != '[' || !input_available(30)) return {Key::escape, {}};
    const char code = read_byte();
    if (code == 'A') return {Key::up, {}};
    if (code == 'B') return {Key::down, {}};
    if (code == 'H') return {Key::home, {}};
    if (code == 'F') return {Key::end, {}};
    if ((code == '1' || code == '4' || code == '5' || code == '6' || code == '7' ||
         code == '8') && input_available(30)) {
        static_cast<void>(read_byte());
        if (code == '1' || code == '7') return {Key::home, {}};
        if (code == '4' || code == '8') return {Key::end, {}};
        return {code == '5' ? Key::page_up : Key::page_down, {}};
    }
    return {Key::escape, {}};
}

std::size_t terminal_rows() {
    winsize size{};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_row > 0) {
        return static_cast<std::size_t>(size.ws_row);
    }
    return 24;
}

std::size_t terminal_columns() {
    winsize size{};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0) {
        return static_cast<std::size_t>(size.ws_col);
    }
    return 100;
}

std::string clipped(std::string_view value, const std::size_t width) {
    if (value.size() <= width) return std::string{value};
    if (width < 2) return std::string{value.substr(0, width)};
    return std::string{value.substr(0, width - 1)} + "~";
}

std::string padded(std::string value, const std::size_t width) {
    value = clipped(value, width);
    if (value.size() < width) value.append(width - value.size(), ' ');
    return value;
}

std::string catalog_row(const Satellite& satellite, const bool selected,
                        const std::size_t width) {
    const auto marker = satellite.is_earth_orbiting() ? (selected ? "[x]" : "[ ]") : "[-]";
    std::ostringstream ilrs;
    ilrs << std::setfill('0') << std::setw(7) << satellite.ilrs_satellite_id;
    std::ostringstream row;
    row << marker << "  " << std::left << std::setw(21) << clipped(satellite.name, 20);
    if (satellite.norad_catalog_id) row << std::setw(9) << *satellite.norad_catalog_id;
    else row << std::setw(9) << "-";
    row << std::setw(11) << ilrs.str()
        << (satellite.is_earth_orbiting() ? "CPF candidate" : "catalog only");
    return padded(row.str(), width);
}

void render(const SatelliteSelectionModel& model, const std::string_view message,
            const std::size_t page_size, const std::size_t first,
            const std::size_t screen_width) {
    const auto& visible = model.visible();
    const auto selected = model.selected();
    const auto last = std::min(first + page_size, visible.size());

    const bool two_panes = screen_width >= 72;
    const auto catalog_width = two_panes ? std::min<std::size_t>(72, screen_width - 25)
                                         : screen_width;
    const auto selected_width = two_panes ? screen_width - catalog_width - 3 : std::size_t{0};

    std::ostringstream output;
    output << "\x1b[2J\x1b[H"
           << "Arctra - select satellites\n\n"
           << "Type to find: " << model.query() << "_   "
           << (model.query().empty() ? visible.size() : model.match_count())
           << " matches   " << model.selected_count() << " selected\n"
           << padded("Sel  Satellite            NORAD    ILRS       Status", catalog_width);
    if (two_panes) output << " | " << padded("Selected satellites", selected_width);
    output << '\n' << std::string(catalog_width, '-');
    if (two_panes) output << "-+-" << std::string(selected_width, '-');
    output << '\n';

    for (std::size_t row = 0; row < page_size; ++row) {
        const auto index = first + row;
        if (index < last) {
            const auto& satellite = *visible[index];
            const auto line = catalog_row(satellite, model.is_selected(satellite.key), catalog_width);
            if (index == model.cursor()) output << "\x1b[7m" << line << "\x1b[0m";
            else output << line;
        } else {
            output << std::string(catalog_width, ' ');
        }
        if (two_panes) {
            output << " | ";
            if (row < selected.size()) {
                output << padded("[x] " + selected[row]->name, selected_width);
            } else if (row == 0 && selected.empty()) {
                output << padded("(none)", selected_width);
            } else {
                output << std::string(selected_width, ' ');
            }
        }
        output << '\n';
    }
    if (!two_panes && !selected.empty()) {
        output << "Selected: ";
        for (std::size_t index = 0; index < selected.size(); ++index) {
            if (index != 0) output << ", ";
            output << selected[index]->name;
        }
        output << '\n';
    }
    output << "\nType to find  Backspace edit  Up/Down move  Space toggle\n"
           << "Home/End first/last  PgUp/PgDn scroll  Enter predict  Esc cancel";
    if (!message.empty()) output << "   " << message;
    output << std::flush;
    std::cout << output.str() << std::flush;
}

}  // namespace

std::optional<std::vector<Satellite>> select_satellites_in_terminal(
    const SatelliteCatalog& catalog) {
    RawTerminal terminal;
    SatelliteSelectionModel model{catalog};
    std::string message;
    std::size_t first_visible{};

    while (true) {
        const auto rows = terminal_rows();
        const auto page_size = rows > 10 ? rows - 10 : std::size_t{3};
        if (model.cursor() < first_visible) first_visible = model.cursor();
        if (model.cursor() >= first_visible + page_size) {
            first_visible = model.cursor() - page_size + 1;
        }
        render(model, message, page_size, first_visible, terminal_columns());
        message.clear();
        const auto input = read_input();

        if (input.key == Key::up) model.move_up();
        else if (input.key == Key::down) model.move_down();
        else if (input.key == Key::home) model.move_home();
        else if (input.key == Key::end) model.move_end();
        else if (input.key == Key::page_up) model.page_up(page_size);
        else if (input.key == Key::page_down) model.page_down(page_size);
        else if (input.key == Key::backspace) {
            auto query = model.query();
            if (!query.empty()) query.pop_back();
            model.set_query(std::move(query));
        } else if (input.key == Key::escape ||
                   (input.key == Key::character && input.character == 3)) {
            return std::nullopt;
        } else if (input.key == Key::character && input.character == ' ') {
            model.toggle_current();
            model.clear_query();
        } else if (input.key == Key::enter) {
            if (model.selected_count() == 0) message = "Select at least one satellite";
            else return model.selected_satellites();
        } else if (input.key == Key::character &&
                   std::isprint(static_cast<unsigned char>(input.character)) != 0) {
            model.set_query(model.query() + input.character);
        }
    }
}

}  // namespace arctra
