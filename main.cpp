#include "arctra/cpf.h"
#include "arctra/cpf_acquisition.h"
#include "arctra/observatory_file.h"
#include "arctra/pass_finder.h"
#include "arctra/satellite.h"
#include "arctra/satellite_file.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
struct PredictOptions {
    // Parsed user intent; domain validation of station values occurs when its
    // configuration file is loaded.
    std::vector<arctra::Satellite> satellites;
    std::string observatory_file;
    std::chrono::seconds duration{std::chrono::hours{24}};
};

void print_usage(std::ostream& output) {
    output << "Usage:\n  arctra predict (--satellite NAME[,NAME...] | --sat-file FILE) "
              "--observatory FILE [--dur HOURS[h]]\n\n"
           << "Example:\n  arctra predict --satellite lageos1,lageos2,etalon1 "
              "--observatory nyal.ini --dur 24h\n"
           << "  arctra predict --sat-file targets.txt --observatory nyal.ini --dur 24h\n";
}

std::string_view require_value(const int argc, char* argv[], int& index,
                               const std::string_view option) {
    if (index + 1 >= argc) {
        throw std::invalid_argument("missing value for " + std::string{option});
    }
    // Advance the caller's argv cursor so option values are not parsed again as
    // independent arguments.
    return argv[++index];
}

std::chrono::seconds parse_duration(std::string_view text) {
    // Keep the initial CLI intentionally narrow: positive whole hours, with an
    // optional h suffix. That matches the printed report and avoids ambiguity.
    if (text.ends_with('h')) {
        text.remove_suffix(1);
    }
    int hours{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), hours);
    if (text.empty() || error != std::errc{} || end != text.data() + text.size() || hours <= 0) {
        throw std::invalid_argument("duration must be a positive integer number of hours");
    }
    return std::chrono::hours{hours};
}

void add_satellites(std::vector<arctra::Satellite>& result, std::string_view list) {
    // A comma-separated list keeps common multi-target runs shell-friendly.
    while (!list.empty()) {
        const auto separator = list.find(',');
        const auto name = list.substr(0, separator);
        if (name.empty()) {
            throw std::invalid_argument("satellite list contains an empty name");
        }
        const auto satellite = arctra::SatelliteCatalog::find(name);
        if (!satellite) {
            throw std::invalid_argument("unknown satellite: " + std::string{name});
        }
        result.push_back(*satellite);
        if (separator == std::string_view::npos) {
            break;
        }
        list.remove_prefix(separator + 1);
    }
}

PredictOptions parse_predict_options(const int argc, char* argv[]) {
    PredictOptions options;
    // argv[0] is the program and argv[1] is the required subcommand.
    for (int index = 2; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "--satellite") {
            add_satellites(options.satellites, require_value(argc, argv, index, argument));
        } else if (argument == "--sat-file") {
            arctra::append_satellites_from_file(
                options.satellites, require_value(argc, argv, index, argument));
        } else if (argument == "--observatory") {
            options.observatory_file = require_value(argc, argv, index, argument);
        } else if (argument == "--dur") {
            options.duration = parse_duration(require_value(argc, argv, index, argument));
        } else {
            throw std::invalid_argument("unknown option: " + std::string{argument});
        }
    }
    if (options.satellites.empty()) {
        throw std::invalid_argument("--satellite is required");
    }
    if (options.observatory_file.empty()) {
        throw std::invalid_argument("--observatory is required");
    }
    return options;
}

std::string format_time(const arctra::UtcTime time) {
    // Render explicitly in UTC regardless of the host's local time zone.
    const auto clock_time = std::chrono::system_clock::to_time_t(time);
    std::tm utc{};
    gmtime_r(&clock_time, &utc);
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%d %H:%M:%S");
    return output.str();
}

std::string format_event(const std::optional<arctra::PassEvent>& event,
                         const std::string_view boundary_text) {
    return event ? format_time(event->time) : std::string{boundary_text};
}



int run_predict(const PredictOptions& options)
{
    const auto observatory = arctra::load_observatory_ini(options.observatory_file);

    // Capture "now" once so every selected satellite is searched over an
    // identical interval, even if downloading multiple CPF files takes time.
    const auto start = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
    const arctra::PredictionInterval interval{start, options.duration};
    arctra::CpfAcquisition acquisition{".arctra-cache/cpf"};
    arctra::PredictionTable all_passes;

    for(const auto& satellite : options.satellites){
        // Acquisition validates the CPF header before it reaches the parser
        // used for pass finding; find_passes then enforces coverage.
        const auto path = acquisition.acquire(satellite);
        const auto ephemeris = arctra::load_cpf(path);
        auto passes = arctra::find_passes(ephemeris, satellite, observatory, interval);
        all_passes.insert(all_passes.end(), passes.begin(), passes.end());
    }
    
    // Present interleaved multi-satellite results in chronological order.
    std::sort(all_passes.begin(), all_passes.end(), [](const auto& left, const auto& right) {
        return left.culmination.time < right.culmination.time;
    });

    std::cout << "Observatory: " << observatory.name()
              << "   Start: " << format_time(interval.start) << " UTC"
              << "   Duration: "
              << std::chrono::duration_cast<std::chrono::hours>(interval.duration).count()
              << " h   Minimum elevation: " << observatory.minimum_elevation_deg() << " deg\n\n"
              << std::left << std::setw(10) << "Satellite"
              << std::setw(22) << "AOS (UTC)" << std::setw(22) << "Maximum (UTC)"
              << std::setw(22) << "LOS (UTC)" << std::right << std::setw(9) << "Max El"
              << std::setw(12) << "Range km" << '\n'
              << std::string(97, '-') << '\n';
    
    for(const auto& pass : all_passes){
        std::cout << std::left << std::setw(10) << pass.satellite.name
                  << std::setw(22) << format_event(pass.aos, "< interval start")
                  << std::setw(22) << format_time(pass.culmination.time)
                  << std::setw(22) << format_event(pass.los, "> interval end")
                  << std::right << std::fixed << std::setprecision(1)
                  << std::setw(8) << pass.culmination.look.elevation_deg << "°"
                  << std::setprecision(0) << std::setw(11)
                  << pass.culmination.look.slant_range_m / 1000.0 << '\n';
    }

    if (all_passes.empty()) std::cout << "No passes above the elevation threshold.\n";
    return EXIT_SUCCESS;
}
}  // namespace



int main(const int argc, char* argv[])
{
    try
    {
        if (argc == 2 && std::string_view{argv[1]} == "--help") {
            print_usage(std::cout);
            return EXIT_SUCCESS;
        }
        if (argc < 2 || std::string_view{argv[1]} != "predict") {
            print_usage(std::cerr);
            return EXIT_FAILURE;
        }
        return run_predict(parse_predict_options(argc, argv));
    }

    catch(const std::exception& error)
    {
        std::cerr << "arctra: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
