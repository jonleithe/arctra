#include "arctra/coordinates.h"
#include "arctra/cpf.h"
#include "arctra/cpf_acquisition.h"
#include "arctra/satellite.h"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace {
using Json = nlohmann::json;

std::string utc_string(const arctra::UtcTime time) {
    const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(time);
    const auto clock_time = std::chrono::system_clock::to_time_t(seconds);
    std::tm utc{};
    gmtime_r(&clock_time, &utc);
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return output.str();
}

std::pair<arctra::UtcTime, arctra::UtcTime> coverage(const arctra::CpfEphemeris& ephemeris) {
    return {std::max(ephemeris.header().intended_start, ephemeris.positions().front().epoch),
            std::min(ephemeris.header().intended_end, ephemeris.positions().back().epoch)};
}

struct LoadedTarget {
    std::filesystem::path path;
    arctra::CpfEphemeris ephemeris;
};

class GlobeWorker {
public:
    explicit GlobeWorker(std::filesystem::path cache_directory)
        : acquisition_(std::move(cache_directory)) {}

    Json evaluate(const Json& request) {
        std::unordered_map<std::string, std::string> load_errors;
        for (const auto& requested : request.at("targets")) {
            const arctra::Satellite satellite{
                requested.at("key").get<std::string>(), requested.at("name").get<std::string>(),
                requested.at("noradId").get<int>(), requested.at("ilrsId").get<int>()};
            if (loaded_.contains(satellite.key)) continue;
            try {
                const auto path = acquisition_.acquire(satellite);
                loaded_.emplace(satellite.key,
                                LoadedTarget{path, arctra::load_cpf(path)});
            } catch (const std::exception& error) {
                load_errors.emplace(satellite.key, error.what());
            }
        }
        // Acquisition may take several seconds. Capture the frame epoch only
        // after it completes so the renderer never receives a newly generated
        // frame that is already stale.
        const auto now = std::chrono::time_point_cast<arctra::UtcTime::duration>(
            std::chrono::system_clock::now());
        const auto& observer = request.at("observer");
        const arctra::GeodeticPosition site{
            observer.at("latitudeDeg").get<double>(),
            observer.at("longitudeDeg").get<double>(),
            observer.at("ellipsoidalHeightM").get<double>()};
        const double minimum_elevation = observer.at("minElevationDeg").get<double>();
        if (!std::isfinite(site.latitude_deg) || std::abs(site.latitude_deg) > 90.0 ||
            !std::isfinite(site.longitude_deg) || std::abs(site.longitude_deg) > 180.0 ||
            !std::isfinite(site.ellipsoidal_height_m) || !std::isfinite(minimum_elevation) ||
            std::abs(minimum_elevation) > 90.0) {
            throw std::invalid_argument("invalid observer coordinates or minimum elevation");
        }

        Json result{{"schemaVersion", 1}, {"ok", true}, {"epochUtc", utc_string(now)},
                    {"observer", {{"latitudeDeg", site.latitude_deg},
                                  {"longitudeDeg", site.longitude_deg},
                                  {"ellipsoidalHeightM", site.ellipsoidal_height_m},
                                  {"minElevationDeg", minimum_elevation}}},
                    {"targets", Json::array()}};

        for (const auto& requested : request.at("targets")) {
            const arctra::Satellite satellite{
                requested.at("key").get<std::string>(), requested.at("name").get<std::string>(),
                requested.at("noradId").get<int>(), requested.at("ilrsId").get<int>()};
            Json state{{"key", satellite.key}, {"name", satellite.name}, {"state", "loading"}};
            try {
                if (const auto error = load_errors.find(satellite.key); error != load_errors.end()) {
                    throw std::runtime_error(error->second);
                }
                auto loaded = loaded_.find(satellite.key);
                if (loaded == loaded_.end()) {
                    throw std::runtime_error("CPF was not loaded");
                }
                const auto& ephemeris = loaded->second.ephemeris;
                const auto [start, end] = coverage(ephemeris);
                const auto filename = loaded->second.path.filename().string();
                state["cpf"] = {{"filename", filename},
                                {"provider", ephemeris.header().ephemeris_source.empty()
                                                 ? "EDC" : ephemeris.header().ephemeris_source},
                                {"referenceFrame", 0},
                                {"usableStartUtc", utc_string(start)},
                                {"usableEndUtc", utc_string(end)},
                                {"sourceUrl", "https://edc.dgfi.tum.de/pub/slr/cpf_predicts_v2/current/" + filename}};
                if (now < start || now > end) {
                    state["state"] = now > end ? "expired" : "outside-coverage";
                    state["message"] = "Current epoch is outside usable CPF coverage";
                } else {
                    const auto position = ephemeris.interpolate(now);
                    const auto look = arctra::topocentric_look_angle(site, position);
                    state["state"] = "ready";
                    state["positionEcefM"] = {position.x_m, position.y_m, position.z_m};
                    state["look"] = {{"azimuthDeg", look.azimuth_deg},
                                     {"elevationDeg", look.elevation_deg},
                                     {"slantRangeM", look.slant_range_m},
                                     {"aboveHorizon", look.elevation_deg >= 0.0},
                                     {"aboveThreshold", look.elevation_deg >= minimum_elevation}};
                }
            } catch (const std::exception& error) {
                state.erase("cpf");
                state["state"] = "error";
                state["message"] = error.what();
            }
            result["targets"].push_back(std::move(state));
        }
        return result;
    }

private:
    arctra::CpfAcquisition acquisition_;
    std::unordered_map<std::string, LoadedTarget> loaded_;
};
}  // namespace

int main(int argc, char* argv[]) {
    try {
        const std::filesystem::path cache = argc == 2 ? argv[1] : ".arctra-cache/cpf";
        GlobeWorker worker{cache};
        std::string line;
        while (std::getline(std::cin, line)) {
            try {
                const auto request = Json::parse(line);
                if (request.at("schemaVersion") != 1) {
                    throw std::invalid_argument("unsupported globe request schema");
                }
                std::cout << worker.evaluate(request).dump() << '\n' << std::flush;
            } catch (const std::exception& error) {
                std::cout << Json{{"schemaVersion", 1}, {"ok", false}, {"error", error.what()}}.dump()
                          << '\n' << std::flush;
            }
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "arctra globe worker: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
