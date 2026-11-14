#include "arctra/live_tracker.h"

#include "arctra/coordinates.h"
#include "arctra/cpf_acquisition.h"
#include "arctra/satellite_selector.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <future>
#include <iomanip>
#include <iostream>
#include <locale>
#include <optional>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

namespace arctra {
namespace {

constexpr double speed_of_light_m_per_s = 299'792'458.0;

class RawTerminal {
public:
    RawTerminal() {
        if (::isatty(STDIN_FILENO) == 0 || ::isatty(STDOUT_FILENO) == 0) {
            throw std::invalid_argument("track requires an interactive terminal");
        }
        if (::tcgetattr(STDIN_FILENO, &original_) != 0) {
            throw std::runtime_error("cannot read terminal settings: " +
                                     std::string{std::strerror(errno)});
        }
        auto raw = original_;
        raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO | ISIG));
        raw.c_iflag &= static_cast<tcflag_t>(~(IXON | ICRNL));
        raw.c_cc[VMIN] = 0;
        raw.c_cc[VTIME] = 0;
        if (::tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) != 0) {
            throw std::runtime_error("cannot enable live tracking terminal mode: " +
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

enum class Key { character, up, down, home, end, page_up, page_down, backspace, tab, quit };

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
    if (first == 'q' || first == 'Q' || first == 3) return {Key::quit, {}};
    if (first == '\t') return {Key::tab, {}};
    if (first == 127 || first == '\b') return {Key::backspace, {}};
    if (first != 27) return {Key::character, first};
    if (!input_available(25)) return {Key::quit, {}};
    if (read_byte() != '[' || !input_available(25)) return {Key::quit, {}};
    const char code = read_byte();
    if (code == 'A') return {Key::up, {}};
    if (code == 'B') return {Key::down, {}};
    if (code == 'H') return {Key::home, {}};
    if (code == 'F') return {Key::end, {}};
    if ((code == '1' || code == '4' || code == '5' || code == '6' || code == '7' ||
         code == '8') && input_available(25)) {
        static_cast<void>(read_byte());
        if (code == '1' || code == '7') return {Key::home, {}};
        if (code == '4' || code == '8') return {Key::end, {}};
        return {code == '5' ? Key::page_up : Key::page_down, {}};
    }
    return {Key::character, {}};
}

std::size_t terminal_columns() {
    winsize size{};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_col > 0) {
        return static_cast<std::size_t>(size.ws_col);
    }
    return 100;
}

std::size_t terminal_rows() {
    winsize size{};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) == 0 && size.ws_row > 0) {
        return static_cast<std::size_t>(size.ws_row);
    }
    return 24;
}

std::string format_time(const UtcTime time) {
    const auto clock_time = std::chrono::system_clock::to_time_t(time);
    std::tm utc{};
    gmtime_r(&clock_time, &utc);
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%d %H:%M:%S") << " UTC";
    return output.str();
}

// Generated SGR colour sequences occupy no columns; UTF-8 symbols occupy one.
std::size_t next_display_token(const std::string_view text, std::size_t offset) {
    if (text[offset] == '\x1b' && offset + 1 < text.size() && text[offset + 1] == '[') {
        const auto end = text.find('m', offset + 2);
        if (end != std::string_view::npos) return end + 1;
    }
    ++offset;
    while (offset < text.size() && (static_cast<unsigned char>(text[offset]) & 0xc0) == 0x80) ++offset;
    return offset;
}

std::size_t character_count(const std::string_view value) {
    std::size_t columns = 0;
    for (std::size_t i = 0; i < value.size();) {
        if (value[i] != '\x1b') ++columns;
        i = next_display_token(value, i);
    }
    return columns;
}

std::string clipped(std::string_view value, const std::size_t width) {
    if (character_count(value) <= width) return std::string{value};
    if (width == 0) return {};
    std::size_t offset = 0;
    std::size_t columns = 0;
    while (offset < value.size() && columns < width - 1) {
        if (value[offset] != '\x1b') ++columns;
        offset = next_display_token(value, offset);
    }
    return std::string{value.substr(0, offset)} + "~\x1b[39m";
}

std::string padded(std::string value, const std::size_t width) {
    value = clipped(value, width);
    const auto count = character_count(value);
    if (count < width) value.append(width - count, ' ');
    return value;
}

std::string coloured(const std::string_view text, const int colour) {
    return "\x1b[" + std::to_string(colour) + "m" + std::string{text} + "\x1b[39m";
}

class GroupedNumbers : public std::numpunct<char> {
    char do_thousands_sep() const override { return ' '; }
    std::string do_grouping() const override { return "\3"; }
};

std::string grouped_number(const double value, const int decimals) {
    std::ostringstream output;
    output.imbue(std::locale{std::locale::classic(), new GroupedNumbers});
    output << std::fixed << std::setprecision(decimals) << value;
    return output.str();
}

std::string catalog_row(const Satellite& satellite, const bool selected,
                        const std::size_t width) {
    const auto marker = satellite.is_earth_orbiting() ? (selected ? "[x]" : "[ ]") : "[-]";
    if (width < 38) {
        return padded(std::string{marker} + " " + satellite.name, width);
    }
    std::ostringstream row;
    row << marker << " " << std::left << std::setw(19) << clipped(satellite.name, 18);
    if (satellite.norad_catalog_id) row << std::setw(8) << *satellite.norad_catalog_id;
    else row << std::setw(8) << "-";
    row << (satellite.is_earth_orbiting() ? "CPF" : "catalog");
    return padded(row.str(), width);
}

struct DisplaySample {
    std::optional<LiveTrackSample> sample;
    std::string error;
};

struct VisibilityTimer {
    bool evaluated{};
    std::optional<NextVisibilityEvent> event;
    std::string error;
};

struct PendingCpfLoad {
    Satellite satellite;
    bool active{};
};

struct RefreshState {
    UtcTime next_check{};
    bool queued{};
};

UtcTime coverage_end(const CpfEphemeris& ephemeris) {
    return ephemeris.positions().empty() ? ephemeris.header().intended_end
        : std::min(ephemeris.header().intended_end, ephemeris.positions().back().epoch);
}

struct CpfLoadResult {
    Satellite satellite;
    std::optional<LiveCpfTarget> target;
    std::string error;
    bool online{};
};

std::vector<DisplaySample> evaluate_all(const std::vector<LiveCpfTarget>& targets,
                                        const Observatory& observatory,
                                        const UtcTime time) {
    std::vector<DisplaySample> result;
    result.reserve(targets.size());
    for (const auto& target : targets) {
        try {
            result.push_back({evaluate_live_track(target.ephemeris, target.satellite,
                                                  observatory, time), {}});
        } catch (const std::exception& error) {
            result.push_back({std::nullopt, error.what()});
        }
    }
    return result;
}

void refresh_visibility_timers(const std::vector<LiveCpfTarget>& targets,
                               const Observatory& observatory,
                               const UtcTime time,
                               std::vector<VisibilityTimer>& timers) {
    for (std::size_t index = 0; index < targets.size(); ++index) {
        auto& timer = timers[index];
        if (timer.evaluated && (!timer.event || time < timer.event->time)) continue;
        timer = {true, std::nullopt, {}};
        try {
            timer.event = next_visibility_event(targets[index].ephemeris,
                                                targets[index].satellite,
                                                observatory, time);
        } catch (const std::exception& error) {
            timer.error = error.what();
        }
    }
}

std::string format_countdown(const UtcTime now, const UtcTime event) {
    const auto remaining = event > now ? event - now : UtcTime::duration::zero();
    const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(remaining).count();
    const auto hours = seconds / 3600;
    const auto minutes = (seconds % 3600) / 60;
    const auto remainder = seconds % 60;
    std::ostringstream output;
    output << std::setfill('0') << std::setw(2) << hours << ':' << std::setw(2) << minutes
           << ':' << std::setw(2) << remainder;
    return output.str();
}

std::string cpf_expiry_label(const UtcTime now, const CpfEphemeris& ephemeris) {
    const auto end = coverage_end(ephemeris);
    if (now >= end) return coloured("CPF EXPIRED", 31);
    if (end - now <= std::chrono::minutes{30}) {
        return coloured("CPF EXPIRING " + format_countdown(now, end), 33);
    }
    return coloured("CPF OK", 32);
}

std::vector<std::size_t> live_display_order(const std::vector<DisplaySample>& samples,
                                            const std::vector<VisibilityTimer>& timers) {
    std::vector<std::size_t> indices(samples.size());
    for (std::size_t index = 0; index < indices.size(); ++index) indices[index] = index;

    const auto group = [&samples, &timers](const std::size_t index) {
        if (!samples[index].sample) return 3;
        if (samples[index].sample->above_minimum_elevation) return 0;
        if (timers[index].event &&
            timers[index].event->kind == VisibilityEventKind::rises_above_threshold) {
            return 1;
        }
        return 2;
    };
    const auto event_time = [&timers](const std::size_t index) {
        return timers[index].event ? timers[index].event->time : UtcTime::max();
    };

    // A stable sort preserves the user's selection order for otherwise equal
    // targets while moving immediately useful tracking data to the top.
    std::stable_sort(indices.begin(), indices.end(), [&](const std::size_t left,
                                                          const std::size_t right) {
        if (group(left) != group(right)) return group(left) < group(right);
        if (group(left) < 2 && event_time(left) != event_time(right)) {
            return event_time(left) < event_time(right);
        }
        return false;
    });
    return indices;
}

void render(const SatelliteSelectionModel& model,
            const std::vector<LiveCpfTarget>& targets,
            const std::vector<DisplaySample>& samples,
            const std::vector<VisibilityTimer>& timers,
            const std::vector<std::size_t>& display_order,
            const std::vector<PendingCpfLoad>& pending_loads,
            const std::unordered_map<std::string, std::string>& load_errors,
            const Observatory& observatory,
            const UtcTime time,
            const std::size_t page_size,
            const std::size_t first_visible,
            const std::string_view message,
            const bool live_focus,
            const std::size_t live_first,
            const std::size_t live_cursor,
            const std::unordered_map<std::string, RefreshState>& refresh_states) {
    const auto width = terminal_columns();
    const auto list_width = std::min<std::size_t>(38, width / 3);
    const auto detail_width = width > list_width + 3 ? width - list_width - 3 : 0;
    std::ostringstream output;
    output << "\x1b[2J\x1b[H"
           << "Arctra live tracker  " << observatory.name() << "  " << format_time(time) << "\n"
           << "Visibility threshold: " << std::fixed << std::setprecision(1)
           << observatory.minimum_elevation_deg() << "°\n\n";

    if (detail_width == 0) {
        output << "Terminal is too narrow for the live tracker.\n";
        std::cout << output.str() << std::flush;
        return;
    }

    const auto total = model.selected_count();
    const auto range = total == 0 ? std::string{"0 of 0"}
        : std::to_string(live_first + 1) + "–" +
          std::to_string(std::min(live_first + page_size, total)) + " of " + std::to_string(total);
    output << "Find: " << model.query() << "_  "
           << (model.query().empty() ? model.visible().size() : model.match_count()) << " matches  "
           << total << " selected\n";
    output << padded(live_focus ? "  Catalog (Space select)" : "▶ Catalog (Space select)", list_width)
           << "\x1b[0m | ";
    output << clipped(std::string{live_focus ? "▶ " : "  "} + "Live targets · " + range, detail_width)
           << "\x1b[0m\n"
           << std::string(list_width, '-') << "-+-" << std::string(detail_width, '-') << '\n';

    const auto& visible = model.visible();
    for (std::size_t row = 0; row < page_size; ++row) {
        const auto catalog_index = first_visible + row;
        if (catalog_index < visible.size()) {
            const auto& satellite = *visible[catalog_index];
            const auto catalog_text = catalog_row(satellite, model.is_selected(satellite.key), list_width);
            if (!live_focus && catalog_index == model.cursor()) output << "\x1b[7m" << catalog_text << "\x1b[0m";
            else output << catalog_text;
        } else {
            output << std::string(list_width, ' ');
        }

        output << " | ";
        const auto live_row = live_first + row;
        if (live_focus && live_row == live_cursor && live_row < total) output << "\x1b[7m";
        if (live_row >= display_order.size() + pending_loads.size()) {
            const auto selected = model.selected();
            std::vector<const Satellite*> failed;
            for (const auto* satellite : selected) {
                const auto is_loaded = std::any_of(targets.begin(), targets.end(), [satellite](const auto& target) {
                    return target.satellite.key == satellite->key;
                });
                const auto is_pending = std::any_of(pending_loads.begin(), pending_loads.end(), [satellite](const auto& pending) {
                    return pending.satellite.key == satellite->key;
                });
                if (!is_loaded && !is_pending && load_errors.contains(satellite->key)) {
                    failed.push_back(satellite);
                }
            }
            const auto failure_index = live_row - display_order.size() - pending_loads.size();
            if (failure_index < failed.size()) {
                output << clipped(coloured("CPF UNAVAILABLE", 31) + " for " + failed[failure_index]->name + "; retry pending", detail_width);
            } else if (live_row == 0 && targets.empty() && pending_loads.empty()) {
                output << "Select an Earth-orbiting target with Space";
            }
            output << "\x1b[0m\n";
            continue;
        }
        if (live_row >= display_order.size()) {
            const auto& pending = pending_loads[live_row - display_order.size()];
            output << clipped(std::string{pending.active ? "Loading CPF for " : "Queued CPF for "} +
                                  pending.satellite.name + "...",
                              detail_width)
                   << "\x1b[0m\n";
            continue;
        }
        const auto index = display_order[live_row];
        const auto& display = samples[index];
        if (!display.sample || time >= coverage_end(targets[index].ephemeris)) {
            output << clipped(padded(targets[index].satellite.name, 18) + coloured("UNKNOWN", 90) + " | " +
                                  cpf_expiry_label(time, targets[index].ephemeris) + " | " + display.error +
                                  (refresh_states.at(targets[index].satellite.key).queued
                                      ? " [refresh queued/loading]" : " [retry pending]"),
                              detail_width);
        } else {
            const auto& sample = *display.sample;
            std::ostringstream data;
            data << std::left << std::setw(18) << clipped(targets[index].satellite.name, 17)
                 << coloured(padded(sample.above_minimum_elevation ? "ABOVE" : "below", 8),
                             sample.above_minimum_elevation ? 32 : 90)
                 << cpf_expiry_label(time, targets[index].ephemeris) << "  "
                 << std::right << std::fixed << std::setprecision(4)
                 << " Az " << std::setw(8) << sample.look.azimuth_deg << "°"
                 << "  El " << std::setw(8) << sample.look.elevation_deg << "°"
                 << "  " << std::setprecision(0) << std::setw(8)
                 << grouped_number(sample.look.slant_range_m / 1000.0, 0) << " km"
                 << "  " << std::setprecision(1) << std::setw(10)
                 << grouped_number(sample.round_trip_light_time_us, 1) << " μs(RT)";
            const auto& timer = timers[index];
            if (timer.event) {
                data << "  "
                     << (timer.event->kind == VisibilityEventKind::rises_above_threshold
                             ? "rises in "
                             : " sets in ")
                     << format_countdown(time, timer.event->time);
            } else if (timer.error.empty()) {
                data << "  no rise within CPF coverage";
            } else {
                data << "  timer unavailable";
            }

            const auto state = refresh_states.find(targets[index].satellite.key);
            if (state != refresh_states.end() && state->second.queued) data << " [refresh queued/loading]";
            else if (load_errors.contains(targets[index].satellite.key)) data << coloured(" [refresh failed; retry pending]", 33);
            output << clipped(data.str(), detail_width);
        }
        output << "\x1b[0m\n";
    }

    const Satellite* focused = nullptr;
    if (live_focus && live_cursor < display_order.size()) focused = &targets[display_order[live_cursor]].satellite;
    else if (!live_focus && !model.visible().empty()) focused = model.visible()[model.cursor()];
    std::ostringstream status;
    if (focused && model.is_selected(focused->key)) {
        status << focused->name << " | ";
        const auto target = std::find_if(targets.begin(), targets.end(), [&](const auto& item) {
            return item.satellite.key == focused->key;
        });
        if (target != targets.end()) {
            status << cpf_expiry_label(time, target->ephemeris) << " | ";
            if (live_focus) {
                status << "expires " << format_time(coverage_end(target->ephemeris)) << " | ";
            }
        }
        const auto state = refresh_states.find(focused->key);
        if (state != refresh_states.end() && state->second.queued) status << "refresh queued/loading";
        else if (state != refresh_states.end()) {
            if (load_errors.contains(focused->key)) status << coloured("refresh failed; ", target != targets.end() && time >= coverage_end(target->ephemeris) ? 31 : 33);
            status << "next check " << format_time(state->second.next_check);
        } else status << "queued";
    } else status << "CPF checks: hourly; retries: 5 min; current data retained while usable";
    output << '\n' << clipped(status.str(), width);

    output << (live_focus
        ? "\nTab: catalog  Up/Down: scroll  Home/End: first/last\n"
        : "\nTab: live pane  Type to find  Space: select/deselect  Up/Down: move\n")
           << "PgUp/PgDn: page  q/Esc: quit  Refresh: 1 s"
           << (message.empty() ? "" : "   " + std::string{message}) << "\n"
           << "Light times are geometric vacuum values from CPF range, not corrected SLR timing.\n";
    std::cout << output.str() << std::flush;
}

}  // namespace

UtcTime next_cpf_refresh(const UtcTime now, const UtcTime end, const bool successful) {
    using namespace std::chrono_literals;
    if (!successful) return now + 5min;
    return std::max(now + 5min, std::min(now + 1h, end - 30min));
}

LiveTrackSample evaluate_live_track(const CpfEphemeris& ephemeris,
                                    const Satellite& satellite,
                                    const Observatory& observatory,
                                    const UtcTime time) {
    const auto look = topocentric_look_angle(observatory.position(), ephemeris.interpolate(time));
    const auto one_way_light_time_us = look.slant_range_m / speed_of_light_m_per_s * 1'000'000.0;
    return {satellite, look, look.elevation_deg >= observatory.minimum_elevation_deg(),
            one_way_light_time_us, 2.0 * one_way_light_time_us};
}

std::optional<NextVisibilityEvent> next_visibility_event(const CpfEphemeris& ephemeris,
                                                          const Satellite& satellite,
                                                          const Observatory& observatory,
                                                          const UtcTime time) {
    if (time < ephemeris.header().intended_start || time >= ephemeris.header().intended_end) {
        throw std::out_of_range("current time is outside CPF intended interval");
    }
    // Header intent can extend beyond the supplied position records. Search
    // only supported epochs, and return the first crossing before inspecting
    // later data (which may contain an unrelated gap).
    const auto& positions = ephemeris.positions();
    if (positions.empty()) throw std::runtime_error("CPF contains no position records");
    const auto end = std::min(ephemeris.header().intended_end, positions.back().epoch);
    const auto above_at = [&](const UtcTime epoch) {
        return evaluate_live_track(ephemeris, satellite, observatory, epoch)
            .above_minimum_elevation;
    };
    auto previous_time = time;
    const bool initially_above = above_at(time);
    while (previous_time < end) {
        const auto next = std::min(previous_time + std::chrono::seconds{30}, end);
        if (above_at(next) != initially_above) {
            auto lower = previous_time;
            auto upper = next;
            while (upper - lower > std::chrono::milliseconds{10}) {
                const auto middle = lower + (upper - lower) / 2;
                if (above_at(middle) == initially_above) lower = middle;
                else upper = middle;
            }
            return NextVisibilityEvent{
                initially_above ? VisibilityEventKind::sets_below_threshold
                                : VisibilityEventKind::rises_above_threshold,
                lower + (upper - lower) / 2};
        }
        previous_time = next;
    }
    return std::nullopt;
}

void run_live_tracker(const SatelliteCatalog& catalog,
                      std::vector<Satellite> initial_satellites,
                      const Observatory& observatory) {
    RawTerminal terminal;
    SatelliteSelectionModel model{catalog};
    std::vector<LiveCpfTarget> targets;
    std::vector<VisibilityTimer> timers;
    std::vector<PendingCpfLoad> pending_loads;
    std::unordered_map<std::string, std::string> load_errors;
    std::unordered_map<std::string, RefreshState> refresh_states;
    std::optional<std::future<CpfLoadResult>> active_load;
    std::string message;
    std::size_t first_visible{};
    std::size_t live_first{};
    std::size_t live_cursor{};
    bool live_focus = false;

    // Do this before starting the loader so the catalog reflects requested
    // priorities immediately. select_key also ignores duplicates and
    // catalog-only (for example lunar) entries.
    for (const auto& satellite : initial_satellites) {
        if (model.is_selected(satellite.key)) continue;
        model.select_key(satellite.key);
        if (satellite.is_earth_orbiting()) pending_loads.push_back({satellite, false});
    }

    const auto start_next_load = [&] {
        if (active_load) return;
        const auto next = std::find_if(pending_loads.begin(), pending_loads.end(),
                                       [](const auto& pending) { return !pending.active; });
        if (next == pending_loads.end()) return;
        next->active = true;
        refresh_states[next->satellite.key].queued = true;
        const auto satellite = next->satellite;
        active_load.emplace(std::async(std::launch::async, [satellite] {
            try {
                CpfAcquisition acquisition{".arctra-cache/cpf"};
                bool online = true;
                std::filesystem::path path;
                try {
                    path = acquisition.acquire(satellite, false);
                } catch (...) {
                    online = false;
                    path = acquisition.acquire(satellite);
                }
                auto ephemeris = load_cpf(path);
                const auto now = std::chrono::time_point_cast<UtcTime::duration>(std::chrono::system_clock::now());
                (void)ephemeris.interpolate(now); // Never install expired or future-only predictions.
                return CpfLoadResult{satellite, LiveCpfTarget{satellite, std::move(ephemeris)}, {}, online};
            } catch (const std::exception& error) {
                return CpfLoadResult{satellite, std::nullopt, error.what()};
            }
        }));
    };

    const auto collect_completed_load = [&] {
        if (!active_load || active_load->wait_for(std::chrono::seconds::zero()) !=
                                std::future_status::ready) {
            return;
        }
        auto result = active_load->get();
        active_load.reset();
        pending_loads.erase(std::remove_if(pending_loads.begin(), pending_loads.end(),
                                           [&result](const auto& pending) {
                                               return pending.satellite.key == result.satellite.key;
                                           }),
                            pending_loads.end());
        if (model.is_selected(result.satellite.key)) {
            const auto now = std::chrono::time_point_cast<UtcTime::duration>(std::chrono::system_clock::now());
            auto found = std::find_if(targets.begin(), targets.end(), [&](const auto& target) {
                return target.satellite.key == result.satellite.key;
            });
            auto end = now;
            if (result.target) {
                end = coverage_end(result.target->ephemeris);
                // Keep existing data if an offline fallback would shorten coverage.
                if (found == targets.end()) {
                    targets.push_back(std::move(*result.target));
                    timers.push_back({});
                } else if (end >= coverage_end(found->ephemeris)) {
                    const auto index = static_cast<std::size_t>(found - targets.begin());
                    *found = std::move(*result.target);
                    timers[index] = {};
                }
                if (result.online) load_errors.erase(result.satellite.key);
                else load_errors[result.satellite.key] = "EDC check failed; using cached CPF";
            } else {
                load_errors[result.satellite.key] = result.error;
            }
            refresh_states[result.satellite.key] = {
                next_cpf_refresh(now, end, result.target.has_value() && result.online), false};
        }
        start_next_load();
    };

    while (true) {
        collect_completed_load();
        start_next_load();
        const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now());
        for (const auto* satellite : model.selected()) {
            auto state = refresh_states.find(satellite->key);
            const bool pending = std::any_of(pending_loads.begin(), pending_loads.end(), [&](const auto& load) {
                return load.satellite.key == satellite->key;
            });
            if (!pending && state != refresh_states.end() && now >= state->second.next_check) {
                pending_loads.push_back({*satellite, false});
                state->second.queued = true;
            }
        }
        start_next_load();
        const auto samples = evaluate_all(targets, observatory, now);
        refresh_visibility_timers(targets, observatory, now, timers);
        const auto display_order = live_display_order(samples, timers);
        const auto rows = terminal_rows();
        const auto page_size = rows > 12 ? rows - 12 : std::size_t{3};
        if (model.cursor() < first_visible) first_visible = model.cursor();
        if (model.cursor() >= first_visible + page_size) {
            first_visible = model.cursor() - page_size + 1;
        }
        const auto live_total = model.selected_count();
        const auto live_max = live_total > page_size ? live_total - page_size : 0;
        live_cursor = std::min(live_cursor, live_total == 0 ? 0 : live_total - 1);
        if (live_cursor < live_first) live_first = live_cursor;
        if (live_cursor >= live_first + page_size) live_first = live_cursor - page_size + 1;
        live_first = std::min(live_first, live_max);
        auto visible_pending = pending_loads;
        std::erase_if(visible_pending, [&](const auto& load) {
            return std::any_of(targets.begin(), targets.end(), [&](const auto& target) {
                return target.satellite.key == load.satellite.key;
            });
        });
        render(model, targets, samples, timers, display_order, visible_pending, load_errors,
               observatory, now, page_size, first_visible, message, live_focus, live_first, live_cursor, refresh_states);
        message.clear();

        if (!input_available(1000)) continue;
        const auto input = read_input();
        if (input.key == Key::quit) return;
        if (input.key == Key::tab) {
            live_focus = !live_focus;
            continue;
        }
        if (live_focus) {
            const auto last = live_total == 0 ? 0 : live_total - 1;
            if (input.key == Key::up && live_cursor > 0) --live_cursor;
            else if (input.key == Key::down) live_cursor = std::min(live_cursor + 1, last);
            else if (input.key == Key::page_up) live_cursor -= std::min(live_cursor, page_size);
            else if (input.key == Key::page_down) live_cursor = std::min(live_cursor + page_size, last);
            else if (input.key == Key::home) live_cursor = 0;
            else if (input.key == Key::end) live_cursor = last;
            continue;
        }
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
        } else if (input.key == Key::character && input.character == ' ') {
            const auto& satellite = *model.visible()[model.cursor()];
            if (!satellite.is_earth_orbiting()) {
                message = "Catalog-only target: no Earth-orbiting CPF path";
            } else if (model.is_selected(satellite.key)) {
                const auto found = std::find_if(targets.begin(), targets.end(), [&satellite](const auto& target) {
                    return target.satellite.key == satellite.key;
                });
                if (found != targets.end()) {
                    const auto index = static_cast<std::size_t>(found - targets.begin());
                    targets.erase(found);
                    timers.erase(timers.begin() + static_cast<std::ptrdiff_t>(index));
                }
                pending_loads.erase(std::remove_if(pending_loads.begin(), pending_loads.end(),
                                                   [&satellite](const auto& pending) {
                                                       return pending.satellite.key == satellite.key;
                                                   }),
                                    pending_loads.end());
                load_errors.erase(satellite.key);
                refresh_states.erase(satellite.key);
                model.toggle_current();
                message = "Stopped " + satellite.name;
            } else {
                model.toggle_current();
                load_errors.erase(satellite.key);
                pending_loads.push_back({satellite, false});
                start_next_load();
            }
            model.clear_query();
        } else if (input.key == Key::character &&
                   std::isprint(static_cast<unsigned char>(input.character)) != 0) {
            model.set_query(model.query() + input.character);
        }
    }
}

}  // namespace arctra
