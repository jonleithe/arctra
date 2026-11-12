#include "arctra/cpf_acquisition.h"

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
constexpr std::string_view base_url = "https://edc.dgfi.tum.de";
constexpr std::string_view current_path = "/pub/slr/cpf_predicts_v2/current/";

size_t append_response(void* data, const size_t size, const size_t count, void* destination) {
    const auto bytes = size * count;
    static_cast<std::string*>(destination)->append(static_cast<char*>(data), bytes);
    return bytes;
}

std::string get(const std::string& url) {
    static const bool curl_initialized = [] {
        return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
    }();
    if (!curl_initialized) throw std::runtime_error("failed to initialize libcurl globally");
    CURL* handle = curl_easy_init();
    if (handle == nullptr) throw std::runtime_error("failed to initialize libcurl");
    std::string response;
    curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
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
    if (filename.ends_with(".dgf")) return 3;
    if (filename.ends_with(".hts")) return 2;
    return 1;
}

std::string issue_key(const std::string& filename) {
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
}  // namespace

CpfAcquisition::CpfAcquisition(std::filesystem::path cache_directory)
    : cache_directory_(std::move(cache_directory)) {}

std::filesystem::path CpfAcquisition::acquire(const Satellite& satellite) const {
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
        if (!cached.empty()) return cache_directory_ / select_newest(std::move(cached));
        throw;
    }

    const std::regex link_pattern{"href=['\"]([^'\"]*" + prefix + "[^'\"]+)['\"]"};
    std::vector<std::string> available;
    for (std::sregex_iterator match{listing.begin(), listing.end(), link_pattern}, end;
         match != end; ++match) {
        available.push_back(std::filesystem::path{(*match)[1].str()}.filename().string());
    }
    const auto filename = select_newest(std::move(available));
    const auto destination = cache_directory_ / filename;
    if (std::filesystem::exists(destination)) return destination;

    const auto contents = get(std::string{base_url} + std::string{current_path} + filename);
    const auto temporary = destination.string() + ".part";
    {
        std::ofstream output{temporary, std::ios::binary};
        if (!output || !(output << contents)) throw std::runtime_error("cannot write CPF cache file");
    }
    std::filesystem::rename(temporary, destination);
    return destination;
}

}  // namespace arctra
