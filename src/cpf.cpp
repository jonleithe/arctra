#include "arctra/cpf.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace arctra {
namespace {

[[noreturn]] void parse_error(const std::size_t line, const std::string& message) {
    // Including the physical line number makes malformed provider files
    // diagnosable without reproducing the parse in a debugger.
    throw std::runtime_error("CPF line " + std::to_string(line) + ": " + message);
}

UtcTime calendar_time(const int year, const unsigned month, const unsigned day,
                      const int hour, const int minute, const int second,
                      const std::size_t line) {
    const std::chrono::year_month_day date{
        std::chrono::year{year}, std::chrono::month{month}, std::chrono::day{day}};
    if (!date.ok() || hour < 0 || hour > 23 || minute < 0 || minute > 59 ||
        second < 0 || second > 60) {
        parse_error(line, "invalid calendar epoch");
    }
    // std::chrono::sys_time has a continuous POSIX-style representation. A
    // parsed second value of 60 therefore normalizes to the following minute;
    // see the UtcTime contract before using this near a leap second.
    return std::chrono::time_point_cast<std::chrono::milliseconds>(
        std::chrono::sys_days{date} + std::chrono::hours{hour} +
        std::chrono::minutes{minute} + std::chrono::seconds{second});
}

UtcTime mjd_time(const int mjd, const double seconds_of_day, const std::size_t line) {
    if (mjd < 0 || !std::isfinite(seconds_of_day) || seconds_of_day < 0.0 ||
        seconds_of_day > 86400.0) {
        parse_error(line, "invalid MJD or seconds-of-day");
    }
    // MJD 0 is 1858-11-17 00:00:00. CPF type-10 epochs use MJD plus UTC
    // seconds of day, rather than a calendar timestamp.
    constexpr std::chrono::sys_days mjd_epoch =
        std::chrono::year{1858} / std::chrono::November / 17;
    const auto offset = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::duration<double>{seconds_of_day});
    return mjd_epoch + std::chrono::days{mjd} + offset;
}

struct ParsedH2 {
    int ilrs_id, sic, norad_id;
    UtcTime start, end;
    int step, reference_frame;
};

ParsedH2 parse_h2(std::istringstream& fields, const std::size_t line) {
    int sy, sm, sd, sh, smin, ss, ey, em, ed, eh, emin, es;
    int compatibility, target_class, rotation_type, center_of_mass, target_location;
    ParsedH2 result{};
    // H2 starts with target identities, then intended interval and step, then
    // compatibility/class/frame/rotation/correction/location flags.
    if (!(fields >> result.ilrs_id >> result.sic >> result.norad_id >>
          sy >> sm >> sd >> sh >> smin >> ss >> ey >> em >> ed >> eh >> emin >> es >>
          result.step >> compatibility >> target_class >> result.reference_frame >>
          rotation_type >> center_of_mass >> target_location)) {
        parse_error(line, "malformed H2 record");
    }
    result.start = calendar_time(sy, static_cast<unsigned>(sm), static_cast<unsigned>(sd),
                                 sh, smin, ss, line);
    result.end = calendar_time(ey, static_cast<unsigned>(em), static_cast<unsigned>(ed),
                               eh, emin, es, line);
    if (result.end <= result.start) parse_error(line, "H2 end must follow start");
    // The downstream geometry assumes an Earth-orbiting passive reflector at a
    // common geocentric epoch. Other CPF target types require different
    // pointing and/or light-time treatment.
    if (target_class != 1 || target_location != 1)
        parse_error(line, "only passive Earth-orbiting targets are supported");
    // ECEF-to-ENU is valid directly only for body-fixed coordinates. Inertial
    // CPF frames require an Earth-orientation transformation not yet provided.
    if (result.reference_frame != 0)
        parse_error(line, "only geocentric true body-fixed (reference frame 0) is supported");
    return result;
}

}  // namespace

CpfEphemeris::CpfEphemeris(CpfHeader header, std::vector<CpfPositionRecord> positions)
    : header_(std::move(header)), positions_(std::move(positions)) {}

const CpfHeader& CpfEphemeris::header() const noexcept { return header_; }
const std::vector<CpfPositionRecord>& CpfEphemeris::positions() const noexcept {
    return positions_;
}

EarthFixedPosition CpfEphemeris::interpolate(const UtcTime epoch) const {
    // This is the CPF specification baseline: ten positions define a ninth
    // order Lagrange polynomial. It supports variable record spacing.
    constexpr std::size_t point_count = 10;
    if (epoch < header_.intended_start || epoch > header_.intended_end) {
        throw std::out_of_range("requested epoch is outside the CPF intended interval");
    }
    // Find the first sample at or after the requested epoch. Exact samples are
    // returned unchanged, avoiding arithmetic round-off at tabulated epochs.
    const auto found = std::lower_bound(
        positions_.begin(), positions_.end(), epoch,
        [](const CpfPositionRecord& record, const UtcTime value) {
            return record.epoch < value;
        });
    if (found != positions_.end() && found->epoch == epoch) {
        return found->position;
    }
    if (positions_.size() < point_count || found == positions_.begin() ||
        found == positions_.end()) {
        throw std::out_of_range("CPF has insufficient records around requested epoch");
    }

    const auto insertion = static_cast<std::size_t>(found - positions_.begin());
    // Centre the interpolation stencil where possible. Near a valid CPF
    // boundary, use the first or final available complete ten-record window.
    const auto maximum_start = positions_.size() - point_count;
    const auto start = std::min(insertion >= point_count / 2 ? insertion - point_count / 2 : 0,
                                maximum_start);
    const auto first_epoch = positions_[start].epoch;

    if (header_.nominal_step_seconds > 0) {
        // A gap larger than two nominal intervals signals missing data. Do not
        // bridge it with a high-order polynomial, which can look plausible but
        // give a poor direction or range.
        const auto maximum_gap = std::chrono::seconds{header_.nominal_step_seconds * 2};
        for (std::size_t index = start + 1; index < start + point_count; ++index) {
            if (positions_[index].epoch - positions_[index - 1].epoch > maximum_gap) {
                throw std::runtime_error("CPF interpolation window contains a data gap");
            }
        }
    }

    // Shift all times near zero before evaluating the polynomial to keep the
    // Lagrange factors well-conditioned for modern MJD-sized epochs.
    const auto argument = std::chrono::duration<double>{epoch - first_epoch}.count();
    EarthFixedPosition result{};
    for (std::size_t index = start; index < start + point_count; ++index) {
        const auto xi = std::chrono::duration<double>{positions_[index].epoch - first_epoch}.count();
        // L_i(t) = product_{j != i} (t-x_j)/(x_i-x_j). Apply each weight to
        // all three Cartesian components of the type-10 position.
        double weight = 1.0;
        for (std::size_t other = start; other < start + point_count; ++other) {
            if (other == index) continue;
            const auto xj = std::chrono::duration<double>{positions_[other].epoch - first_epoch}.count();
            weight *= (argument - xj) / (xi - xj);
        }
        result.x_m += weight * positions_[index].position.x_m;
        result.y_m += weight * positions_[index].position.y_m;
        result.z_m += weight * positions_[index].position.z_m;
    }
    return result;
}

CpfEphemeris parse_cpf(std::istream& input) {
    std::optional<CpfHeader> header;
    std::optional<std::string> source, target;
    int version{}, production_year{}, production_month{}, production_day{}, production_hour{};
    int sequence{}, sub_sequence{};
    std::vector<CpfPositionRecord> positions;
    bool saw_h9 = false;
    bool saw_trailer = false;

    std::string line_text;
    std::size_t line_number{};
    // CPF v2 is free-format: fields are whitespace separated, and comment or
    // optional records may be interspersed with the required record sequence.
    while (std::getline(input, line_text)) {
        ++line_number;
        std::istringstream fields{line_text};
        std::string type;
        if (!(fields >> type)) continue;
        if (type == "H1") {
            std::string format, parsed_source, parsed_target, notes;
            if (source || !(fields >> format >> version >> parsed_source >> production_year >>
                            production_month >> production_day >> production_hour >> sequence >>
                            sub_sequence >> parsed_target)) {
                parse_error(line_number, "malformed or duplicate H1 record");
            }
            // H1 establishes format version and the provider/target labels.
            // Production time, sequence, and notes are preserved by the file
            // cache name but are not yet exposed in CpfHeader.
            if (format != "CPF" || version != 2) parse_error(line_number, "only CPF version 2 is supported");
            source = std::move(parsed_source);
            target = std::move(parsed_target);
        } else if (type == "H2") {
            if (!source || header) parse_error(line_number, "H2 must follow one H1 record");
            const auto h2 = parse_h2(fields, line_number);
            header = CpfHeader{version, *source, *target, h2.ilrs_id, h2.sic, h2.norad_id,
                               h2.start, h2.end, h2.step, h2.reference_frame};
        } else if (type == "H9") {
            // H9 terminates the headers. Position records before it are not a
            // valid CPF ephemeris sequence.
            if (!header) parse_error(line_number, "H9 encountered before required headers");
            saw_h9 = true;
        } else if (type == "10") {
            int direction{}, mjd{}, leap_flag{};
            double seconds{}, x{}, y{}, z{};
            if (!saw_h9 || !(fields >> direction >> mjd >> seconds >> leap_flag >> x >> y >> z))
                parse_error(line_number, "malformed or misplaced position record");
            // Direction 0 is the geocentre/satellite common epoch used by the
            // Earth-orbiting geometric path. Directions 1 and 2 are light-time
            // legs for targets that need separate transmit/receive treatment.
            if (direction != 0) parse_error(line_number, "only common-epoch direction 0 is supported");
            positions.push_back({mjd_time(mjd, seconds, line_number), leap_flag, {x, y, z}});
        } else if (type == "99") {
            saw_trailer = true;
        } else if (type == "00" || type == "H3" || type == "H4" || type == "H5" ||
                   type == "20" || type == "30" || type == "40" || type == "50" ||
                   type == "60" || type == "70") {
            // Optional records are currently not used for this passive,
            // body-fixed geometry path. Recognize them so valid CPF files are
            // not rejected merely for containing additional metadata.
            continue;
        } else {
            parse_error(line_number, "unknown record type '" + type + "'");
        }
    }
    if (!header || !saw_h9 || !saw_trailer) throw std::runtime_error("CPF is missing required header or trailer records");
    if (positions.empty()) throw std::runtime_error("CPF contains no position records");
    // Interpolation requires a strictly increasing independent variable.
    if (std::adjacent_find(positions.begin(), positions.end(),
                           [](const auto& a, const auto& b) {
                               return b.epoch <= a.epoch;
                           }) != positions.end()) {
        throw std::runtime_error("CPF position epochs are not strictly ordered");
    }
    return CpfEphemeris{std::move(*header), std::move(positions)};
}

CpfEphemeris load_cpf(const std::filesystem::path& path) {
    // Keep parsing usable for downloaded files, supplied local fixtures, and
    // reproducible offline investigations.
    std::ifstream input{path};
    if (!input) throw std::runtime_error("cannot open CPF file: " + path.string());
    return parse_cpf(input);
}

}  // namespace arctra
