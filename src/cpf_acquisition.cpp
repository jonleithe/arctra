#include "arctra/cpf_acquisition.h"

#include "arctra/cpf.h"

#include <curl/curl.h>

#include <algorithm>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace arctra {
namespace {
// EDC provides an unauthenticated directory of current CPF v2 files. Keeping
// the base and path separate makes the request construction auditable.
constexpr std::string_view base_url = "https://edc.dgfi.tum.de";
constexpr std::string_view current_path = "/pub/slr/cpf_predicts_v2/current/";

size_t append_response(void* data, const size_t size, const size_t count, void* destination) {
    // libcurl calls the callback in arbitrary-sized chunks; append rather than
    // assuming a complete HTTP response arrives in one invocation.
    const auto bytes = size * count;
    static_cast<std::string*>(destination)->append(static_cast<char*>(data), bytes);
    return bytes;
}

std::string get(const std::string& url) {
    // libcurl global state is initialized once per process, before any easy
    // handles are created. This CLI currently makes requests serially.
    static const bool curl_initialized = [] {
        return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    }();
    if (!curl_initialized) throw std::runtime_error("failed to initialize libcurl globally");
    CURL* handle = curl_easy_init();
    if (handle == nullptr) throw std::runtime_error("failed to initialize libcurl");
    std::string response;
    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
    // Follow a provider redirect, but convert HTTP 4xx/5xx into a failure so a
    // response page is never mistaken for a CPF file.
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(handle, CURLOPT_USERAGENT, "arctra/0.1");
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, append_response);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response);
    const CURLcode result = curl_easy_perform(handle);
    curl_easy_cleanup(handle);
    if (result != CURLE_OK) {
        throw std::runtime_error("EDC request failed: " + std::string{curl_easy_strerror(result)});
    }
    return response;
}

int provider_rank(const std::string& filename) {
    // If providers issue the same version, prefer DGF, then HTS. This is a
    // deterministic choice, not an assertion of prediction quality.
    if (filename.ends_with(".dgf")) return 3;
    if (filename.ends_with(".hts")) return 2;
    return 1;
}

std::string issue_key(const std::string& filename) {
    // CPF filenames are target_cpf_yymmdd_nnnvv.src. The suffix between
    // "_cpf_" and extension sorts chronologically/version-wise as text.
    const auto marker = filename.find("_cpf_");
    const auto extension = filename.rfind('.');
    return filename.substr(marker + 5, extension - marker - 5);
}

std::string select_newest(std::vector<std::string> candidates) {
    if (candidates.empty()) throw std::runtime_error("EDC has no current CPF for requested satellite");
    return *std::max_element(candidates.begin(), candidates.end(),
        [](const std::string& left, const std::string& right) {
            if (issue_key(left) != issue_key(right)) return issue_key(left) < issue_key(right);
            return provider_rank(left) < provider_rank(right);
        });
}

void validate_cpf_header(const CpfHeader& header, const Satellite& satellite) {
    // Parser checks syntax and supported CPF geometry. Acquisition adds the
    // trust-boundary checks: a filename from a remote listing must not choose a
    // different satellite merely because its name resembles the requested one.
    if (header.format_version != 2) {
        throw std::runtime_error("CPF header is not version 2");
    }
    if (header.norad_catalog_id != satellite.norad_catalog_id) {
        throw std::runtime_error("CPF NORAD ID does not match requested satellite " +
                                 std::string{satellite.name});
    }
    if (header.ilrs_satellite_id != satellite.ilrs_satellite_id) {
        throw std::runtime_error("CPF ILRS ID does not match requested satellite " +
                                 std::string{satellite.name});
    }
    const auto named_satellite = SatelliteCatalog::find(header.target_name);
    if (!named_satellite || named_satellite->key != satellite.key) {
        throw std::runtime_error("CPF target name '" + header.target_name +
                                 "' does not match requested satellite " +
                                 std::string{satellite.name});
    }
}
}  // namespace

void validate_cpf_file(const std::filesystem::path& path, const Satellite& satellite) {
    validate_cpf_header(load_cpf(path).header(), satellite);
}

CpfAcquisition::CpfAcquisition(std::filesystem::path cache_directory)
    : cache_directory_(std::move(cache_directory)) {}

std::filesystem::path CpfAcquisition::acquire(const Satellite& satellite) const {
    // Cache first for resilience, but still ask EDC for its current listing so
    // an existing older file does not hide a newly issued prediction.
    std::filesystem::create_directories(cache_directory_);
    const std::string prefix = std::string{satellite.key} + "_cpf_";
    std::vector<std::string> cached;
    for (const auto& entry : std::filesystem::directory_iterator{cache_directory_}) {
        const auto filename = entry.path().filename().string();
        if (entry.is_regular_file() && filename.starts_with(prefix)) cached.push_back(filename);
    }

    std::string listing;
    try {
        if (!current_listing_) {
            current_listing_ = get(std::string{base_url} + std::string{current_path});
        }
        listing = *current_listing_;
    } catch (const std::exception&) {
        // Offline use is intentional, but a cached file is subject to the same
        // header validation as a newly downloaded one. Coverage is checked by
        // the pass finder against the requested interval.
        if (!cached.empty()) {
            const auto cached_path = cache_directory_ / select_newest(std::move(cached));
            validate_cpf_file(cached_path, satellite);
            return cached_path;
        }
        throw;
    }

    // The public directory is HTML rather than a JSON API. Extract only href
    // targets containing this satellite's exact CPF filename prefix.
    const std::regex link_pattern{"href=['\"]([^'\"]*" + prefix + "[^'\"]+)['\"]"};
    std::vector<std::string> available;
    for (std::sregex_iterator match{listing.begin(), listing.end(), link_pattern}, end;
         match != end; ++match) {
        available.push_back(std::filesystem::path{(*match)[1].str()}.filename().string());
    }
    const auto filename = select_newest(std::move(available));
    const auto destination = cache_directory_ / filename;
    if (std::filesystem::exists(destination)) {
        validate_cpf_file(destination, satellite);
        return destination;
    }

    const auto contents = get(std::string{base_url} + std::string{current_path} + filename);
    // Write and validate a sidecar file first. A malformed download can never
    // replace a verified cache entry or later be mistaken for a usable CPF.
    const auto temporary = destination.string() + ".part";
    {
        std::ofstream output{temporary, std::ios::binary};
        if (!output || !(output << contents)) throw std::runtime_error("cannot write CPF cache file");
    }
    try {
        validate_cpf_file(temporary, satellite);
        std::filesystem::rename(temporary, destination);
    } catch (...) {
        // Best-effort cleanup preserves the original validation exception.
        std::error_code error;
        std::filesystem::remove(temporary, error);
        throw;
    }
    return destination;
}

}  // namespace arctra
