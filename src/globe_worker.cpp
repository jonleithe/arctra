#include "arctra/coordinates.h"
#include "arctra/cpf.h"
#include "arctra/cpf_acquisition.h"
#include "arctra/satellite.h"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <numbers>
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

std::string utc_string_milliseconds(const arctra::UtcTime time) {
    const auto milliseconds = std::chrono::time_point_cast<std::chrono::milliseconds>(time);
    const auto seconds = std::chrono::time_point_cast<std::chrono::seconds>(milliseconds);
    const auto fraction = (milliseconds - seconds).count();
    const auto clock_time = std::chrono::system_clock::to_time_t(seconds);
    std::tm utc{};
    gmtime_r(&clock_time, &utc);
    std::ostringstream output;
    output << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.'
           << std::setfill('0') << std::setw(3) << fraction << 'Z';
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

std::string cache_timestamp(const std::filesystem::path& path) {
    // Cache files are written atomically when retrieved. C++20 file clocks do
    // not have a portable direct conversion, so translate through the two
    // clocks' current instants. Sub-second uncertainty is immaterial here.
    const auto file_now = std::filesystem::file_time_type::clock::now();
    const auto system_now = std::chrono::system_clock::now();
    const auto modified = std::filesystem::last_write_time(path);
    const auto system_modified = std::chrono::time_point_cast<std::chrono::seconds>(
        system_now + (modified - file_now));
    return utc_string(system_modified);
}

Json cpf_orbit_view(const LoadedTarget& loaded,
                    const arctra::UtcTime reference_epoch,
                    const arctra::UtcTime usable_start,
                    const arctra::UtcTime usable_end,
                    const arctra::GeodeticPosition& site,
                    const double minimum_elevation) {
    constexpr auto derivative_step = std::chrono::seconds{30};
    constexpr auto display_step = std::chrono::seconds{30};
    constexpr double earth_rotation_rad_s = 7.2921150e-5;
    constexpr double earth_mu_m3_s2 = 3.986004418e14;

    const auto display_position = [&](const arctra::UtcTime time,
                                      const arctra::EarthFixedPosition& position) {
        const double elapsed = std::chrono::duration<double>{time - reference_epoch}.count();
        const double angle = earth_rotation_rad_s * elapsed;
        const double cosine = std::cos(angle);
        const double sine = std::sin(angle);
        return std::array<double, 3>{cosine * position.x_m - sine * position.y_m,
                                     sine * position.x_m + cosine * position.y_m,
                                     position.z_m};
    };
    if (reference_epoch - derivative_step < usable_start ||
        reference_epoch + derivative_step > usable_end) {
        throw std::runtime_error("CPF lacks samples around the reference epoch for orbit estimation");
    }
    const auto before_ecef = loaded.ephemeris.interpolate(reference_epoch - derivative_step);
    const auto current_ecef = loaded.ephemeris.interpolate(reference_epoch);
    const auto after_ecef = loaded.ephemeris.interpolate(reference_epoch + derivative_step);
    const auto before = display_position(reference_epoch - derivative_step, before_ecef);
    const auto current = display_position(reference_epoch, current_ecef);
    const auto after = display_position(reference_epoch + derivative_step, after_ecef);
    const double derivative_seconds = 2.0 * derivative_step.count();
    const std::array<double, 3> velocity{
        (after[0] - before[0]) / derivative_seconds,
        (after[1] - before[1]) / derivative_seconds,
        (after[2] - before[2]) / derivative_seconds};
    const double radius = std::hypot(current[0], current[1], current[2]);
    const double speed_squared = velocity[0] * velocity[0] + velocity[1] * velocity[1] +
                                 velocity[2] * velocity[2];
    const double specific_energy = speed_squared / 2.0 - earth_mu_m3_s2 / radius;
    const double semi_major_axis = -earth_mu_m3_s2 / (2.0 * specific_energy);
    const double period_seconds = 2.0 * std::numbers::pi *
        std::sqrt(semi_major_axis * semi_major_axis * semi_major_axis / earth_mu_m3_s2);
    if (!std::isfinite(period_seconds) || period_seconds < 2700.0 || period_seconds > 86400.0) {
        throw std::runtime_error("CPF state produced an implausible one-revolution period");
    }
    const auto orbit_end = reference_epoch + std::chrono::duration_cast<arctra::UtcTime::duration>(
        std::chrono::duration<double>{period_seconds});
    if (orbit_end > usable_end) {
        throw std::runtime_error("CPF coverage is too short for one complete revolution");
    }

    struct OrbitPoint {
        arctra::UtcTime epoch;
        arctra::EarthFixedPosition ecef;
        std::array<double, 3> display;
        bool above_threshold;
    };
    const auto evaluate = [&](const arctra::UtcTime time) {
        const auto ecef = loaded.ephemeris.interpolate(time);
        const auto look = arctra::topocentric_look_angle(site, ecef);
        return OrbitPoint{time, ecef, display_position(time, ecef),
                          look.elevation_deg >= minimum_elevation};
    };
    const auto refine_crossing = [&](arctra::UtcTime lower, arctra::UtcTime upper,
                                     const bool rising) {
        while (upper - lower > std::chrono::milliseconds{10}) {
            const auto middle = lower + (upper - lower) / 2;
            if (evaluate(middle).above_threshold == rising) upper = middle;
            else lower = middle;
        }
        return evaluate(lower + (upper - lower) / 2);
    };
    const auto coordinates = [](const OrbitPoint& point) {
        return Json::array({point.display[0], point.display[1], point.display[2]});
    };

    Json positions = Json::array();
    Json visible_segments = Json::array();
    Json active_segment = Json::array();
    auto previous = evaluate(reference_epoch);
    positions.push_back(coordinates(previous));
    if (previous.above_threshold) active_segment.push_back(coordinates(previous));
    for (auto time = reference_epoch + display_step; time < orbit_end; time += display_step) {
        const auto point = evaluate(time);
        if (point.above_threshold != previous.above_threshold) {
            const bool rising = point.above_threshold;
            const auto crossing = refine_crossing(previous.epoch, point.epoch, rising);
            if (rising) {
                active_segment = Json::array({coordinates(crossing)});
            } else {
                active_segment.push_back(coordinates(crossing));
                if (active_segment.size() >= 2) {
                    visible_segments.push_back({{"positionsM", std::move(active_segment)}});
                }
                active_segment = Json::array();
            }
        }
        positions.push_back(coordinates(point));
        if (point.above_threshold) active_segment.push_back(coordinates(point));
        previous = point;
    }
    const auto final_point = evaluate(orbit_end);
    if (final_point.above_threshold != previous.above_threshold) {
        const bool rising = final_point.above_threshold;
        const auto crossing = refine_crossing(previous.epoch, final_point.epoch, rising);
        if (rising) active_segment = Json::array({coordinates(crossing)});
        else {
            active_segment.push_back(coordinates(crossing));
            if (active_segment.size() >= 2) {
                visible_segments.push_back({{"positionsM", std::move(active_segment)}});
            }
            active_segment = Json::array();
        }
    }
    positions.push_back(coordinates(final_point));
    if (final_point.above_threshold) active_segment.push_back(coordinates(final_point));
    if (active_segment.size() >= 2) {
        visible_segments.push_back({{"positionsM", std::move(active_segment)}});
    }

    return {{"model", "cpf-nominal-earth-rotation"},
            {"referenceEpochUtc", utc_string(reference_epoch)},
            {"estimatedPeriodSeconds", period_seconds},
            {"positionsM", std::move(positions)},
            {"aboveThresholdSegments", std::move(visible_segments)}};
}

Json trajectory_snapshot(const arctra::Satellite& satellite,
                         const LoadedTarget& loaded,
                         const arctra::UtcTime epoch,
                         const arctra::UtcTime usable_start,
                         const arctra::UtcTime usable_end,
                         const arctra::GeodeticPosition& site,
                         const double minimum_elevation) {
    constexpr auto step = std::chrono::seconds{30};
    constexpr auto minimum_duration = std::chrono::hours{2};
    constexpr auto maximum_duration = std::chrono::hours{12};

    struct EvaluatedPoint {
        arctra::UtcTime epoch;
        arctra::EarthFixedPosition position;
        bool above_threshold;
    };
    const auto evaluate = [&](const arctra::UtcTime time) {
        const auto position = loaded.ephemeris.interpolate(time);
        const auto look = arctra::topocentric_look_angle(site, position);
        return EvaluatedPoint{time, position, look.elevation_deg >= minimum_elevation};
    };
    const auto sample_json = [](const EvaluatedPoint& point, const bool precise = false) {
        return Json{{"epochUtc", precise ? utc_string_milliseconds(point.epoch)
                                          : utc_string(point.epoch)},
                    {"positionEcefM", {point.position.x_m, point.position.y_m,
                                       point.position.z_m}}};
    };
    const auto refine_crossing = [&](arctra::UtcTime lower, arctra::UtcTime upper,
                                     const bool rising) {
        while (upper - lower > std::chrono::milliseconds{10}) {
            const auto middle = lower + (upper - lower) / 2;
            const bool above = evaluate(middle).above_threshold;
            if (above == rising) upper = middle;
            else lower = middle;
        }
        return evaluate(lower + (upper - lower) / 2);
    };

    const auto hard_end = std::min(usable_end, epoch + maximum_duration);
    Json samples = Json::array();
    Json visible_segments = Json::array();
    Json active_segment = Json::array();
    auto previous = evaluate(epoch);
    samples.push_back(sample_json(previous));
    if (previous.above_threshold) active_segment.push_back(sample_json(previous));
    bool completed_visible_pass = false;
    std::string end_reason = hard_end == usable_end ? "cpf-coverage" : "display-cap";

    for (auto sample_epoch = epoch + step; sample_epoch <= hard_end; sample_epoch += step) {
        EvaluatedPoint current{};
        try {
            current = evaluate(sample_epoch);
        } catch (const std::exception&) {
            // A CPF data gap ends this continuous drawable segment. Never
            // bridge the gap or extrapolate merely to complete the preview.
            end_reason = "data-gap";
            break;
        }

        if (current.above_threshold != previous.above_threshold) {
            const bool rising = current.above_threshold;
            const auto crossing = refine_crossing(previous.epoch, current.epoch, rising);
            if (rising) {
                active_segment = Json::array({sample_json(crossing, true)});
            } else {
                active_segment.push_back(sample_json(crossing, true));
                if (active_segment.size() >= 2) {
                    visible_segments.push_back({{"samples", std::move(active_segment)}});
                }
                active_segment = Json::array();
                completed_visible_pass = true;
            }
        }
        samples.push_back(sample_json(current));
        if (current.above_threshold) active_segment.push_back(sample_json(current));
        previous = current;

        if (completed_visible_pass && sample_epoch >= epoch + minimum_duration) {
            end_reason = "visible-pass-complete";
            break;
        }
    }
    if (samples.size() < 2) {
        throw std::runtime_error("CPF has less than 30 seconds of continuous trajectory coverage");
    }
    if (active_segment.size() >= 2) {
        visible_segments.push_back({{"samples", std::move(active_segment)}});
    }
    const auto filename = loaded.path.filename().string();
    const auto provider = loaded.ephemeris.header().ephemeris_source.empty()
        ? "EDC" : loaded.ephemeris.header().ephemeris_source;
    return {{"schemaVersion", 1},
            {"predictionKind", "cpf-ecef-geometric"},
            {"displayMode", "live"},
            {"generatedAtUtc", utc_string(epoch)},
            {"epochUtc", utc_string(epoch)},
            {"target", {{"key", satellite.key}, {"name", satellite.name},
                         {"noradId", *satellite.norad_catalog_id},
                         {"ilrsId", satellite.ilrs_satellite_id}}},
            {"cpf", {{"provider", provider}, {"filename", filename},
                     {"sourceUrl", "https://edc.dgfi.tum.de/pub/slr/cpf_predicts_v2/current/" + filename},
                     {"retrievedAtUtc", cache_timestamp(loaded.path)}, {"referenceFrame", 0},
                     {"usableStartUtc", utc_string(usable_start)},
                     {"usableEndUtc", utc_string(usable_end)}}},
            {"trajectory", {{"sampleStepSeconds", step.count()},
                            {"samples", std::move(samples)},
                            {"visibility", {
                                {"observer", {{"latitudeDeg", site.latitude_deg},
                                              {"longitudeDeg", site.longitude_deg},
                                              {"ellipsoidalHeightM", site.ellipsoidal_height_m},
                                              {"minElevationDeg", minimum_elevation}}},
                                {"endReason", end_reason},
                                {"aboveThresholdSegments", std::move(visible_segments)}}},
                            {"orbitView", cpf_orbit_view(loaded, epoch, usable_start, usable_end,
                                                        site, minimum_elevation)}}}};
}

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
        const auto now = arctra::UtcTime{std::chrono::time_point_cast<std::chrono::seconds>(
            std::chrono::system_clock::now()).time_since_epoch()};
        const bool include_trajectories = request.value("includeTrajectories", false);
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
        if (include_trajectories) result["snapshots"] = Json::array();

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
                    if (include_trajectories) {
                        try {
                            result["snapshots"].push_back(
                                trajectory_snapshot(satellite, loaded->second, now, start, end,
                                                    site, minimum_elevation));
                        } catch (const std::exception& error) {
                            state["geometryError"] = error.what();
                        }
                    }
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
