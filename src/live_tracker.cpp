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

enum class Key { character, up, down, home, end, page_up, page_down, backspace, quit };

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

struct CpfLoadResult {
    Satellite satellite;
    std::optional<LiveCpfTarget> target;
    std::string error;
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
            const std::string_view message) {
    const auto width = terminal_columns();
    const auto list_width = std::min<std::size_t>(38, width / 3);
    const auto detail_width = width > list_width + 3 ? width - list_width - 3 : 0;
    std::ostringstream output;
    output << "\x1b[2J\x1b[H"
           << "Arctra live tracker  " << observatory.name() << "  " << format_time(time) << "\n"
           << "Minimum elevation: " << std::fixed << std::setprecision(1)
           << observatory.minimum_elevation_deg() << " deg\n\n";

    if (detail_width == 0) {
        output << "Terminal is too narrow for the live tracker.\n";
        std::cout << output.str() << std::flush;
        return;
    }

    output << "Find: " << model.query() << "_  "
           << (model.query().empty() ? model.visible().size() : model.match_count()) << " matches  "
           << model.selected_count() << " selected\n"
           << std::left << std::setw(static_cast<int>(list_width)) << "Catalog (Space select)"
           << " | Satellite          Live geometric data\n"
           << std::string(list_width, '-') << "-+-" << std::string(detail_width, '-') << '\n';

    const auto& visible = model.visible();
    for (std::size_t row = 0; row < page_size; ++row) {
        const auto catalog_index = first_visible + row;
        if (catalog_index < visible.size()) {
            const auto& satellite = *visible[catalog_index];
            const auto catalog_text = catalog_row(satellite, model.is_selected(satellite.key), list_width);
            if (catalog_index == model.cursor()) output << "\x1b[7m" << catalog_text << "\x1b[0m";
            else output << catalog_text;
        } else {
            output << std::string(list_width, ' ');
        }

        output << " | ";
        if (row >= display_order.size() + pending_loads.size()) {
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
            const auto failure_index = row - display_order.size() - pending_loads.size();
            if (failure_index < failed.size()) {
                output << clipped("CPF failed for " + failed[failure_index]->name + "; Space retries", detail_width);
            } else if (row == 0 && targets.empty() && pending_loads.empty()) {
                output << "Select an Earth-orbiting target with Space";
            }
            output << '\n';
            continue;
        }
        if (row >= display_order.size()) {
            const auto& pending = pending_loads[row - display_order.size()];
            output << clipped(std::string{pending.active ? "Loading CPF for " : "Queued CPF for "} +
                                  pending.satellite.name + "...",
                              detail_width)
                   << '\n';
            continue;
        }
        const auto index = display_order[row];
        const auto& display = samples[index];
        if (!display.sample) {
            output << clipped(padded(targets[index].satellite.name, 18) + "Unavailable: " +
                                  display.error,
                              detail_width);
        } else {
            const auto& sample = *display.sample;
            std::ostringstream data;
            data << std::left << std::setw(18) << clipped(targets[index].satellite.name, 17)
                 << std::setw(12) << (sample.above_minimum_elevation ? "ABOVE" : "below")
                 << std::right << std::fixed << std::setprecision(4)
                 << " Az " << std::setw(8) << sample.look.azimuth_deg << " deg"
                 << "  El " << std::setw(8) << sample.look.elevation_deg << " deg"
                 << "  " << std::setprecision(0) << std::setw(8)
                 << sample.look.slant_range_m / 1000.0 << " km"
                 << "  " << std::setprecision(1) << std::setw(10)
                 << sample.round_trip_light_time_us << " us (RT)";
            const auto& timer = timers[index];
            if (timer.event) {
                data << "  "
                     << (timer.event->kind == VisibilityEventKind::rises_above_threshold
                             ? "rises in "
                             : "sets in ")
                     << format_countdown(time, timer.event->time);
            } else if (timer.error.empty()) {
                data << "  no rise within CPF coverage";
            } else {
                data << "  timer unavailable";
            }
            output << clipped(data.str(), detail_width);
        }
        output << '\n';
    }

    output << "\nType to find  Space select/deselect  Up/Down move  Home/End first/last\n"
           << "PgUp/PgDn scroll  q/Esc quit  Refresh: 1 s"
           << (message.empty() ? "" : "   " + std::string{message}) << "\n"
           << "Light times are geometric vacuum values from CPF range, not corrected SLR timing.\n";
    std::cout << output.str() << std::flush;
}

}  // namespace

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
    std::optional<std::future<CpfLoadResult>> active_load;
    std::string message;
    std::size_t first_visible{};

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
        const auto satellite = next->satellite;
        active_load.emplace(std::async(std::launch::async, [satellite] {
            try {
                CpfAcquisition acquisition{".arctra-cache/cpf"};
                const auto path = acquisition.acquire(satellite);
                return CpfLoadResult{satellite, LiveCpfTarget{satellite, load_cpf(path)}, {}};
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
        const auto result = active_load->get();
        active_load.reset();
        pending_loads.erase(std::remove_if(pending_loads.begin(), pending_loads.end(),
                                           [&result](const auto& pending) {
                                               return pending.satellite.key == result.satellite.key;
                                           }),
                            pending_loads.end());
        if (result.target && model.is_selected(result.satellite.key)) {
            targets.push_back(std::move(*result.target));
            timers.push_back({});
        } else if (!result.error.empty() && model.is_selected(result.satellite.key)) {
            load_errors[result.satellite.key] = result.error;
        }
        start_next_load();
    };

    while (true) {
        collect_completed_load();
        start_next_load();
        const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now());
        const auto samples = evaluate_all(targets, observatory, now);
        refresh_visibility_timers(targets, observatory, now, timers);
        const auto display_order = live_display_order(samples, timers);
        const auto rows = terminal_rows();
        const auto page_size = rows > 11 ? rows - 11 : std::size_t{3};
        if (model.cursor() < first_visible) first_visible = model.cursor();
        if (model.cursor() >= first_visible + page_size) {
            first_visible = model.cursor() - page_size + 1;
        }
        render(model, targets, samples, timers, display_order, pending_loads, load_errors,
               observatory, now, page_size, first_visible, message);
        message.clear();

        if (!input_available(1000)) continue;
        const auto input = read_input();
        if (input.key == Key::quit) return;
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
