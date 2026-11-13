#ifndef ARCTRA_CPF_ACQUISITION_H
#define ARCTRA_CPF_ACQUISITION_H

#include "arctra/satellite.h"

#include <filesystem>
#include <optional>
#include <string>

namespace arctra {

// Parse a locally stored CPF and verify that its header identifies the requested
// target. The CPF parser also enforces the currently supported target class,
// Earth-orbit location, and body-fixed reference frame.
void validate_cpf_file(const std::filesystem::path& path, const Satellite& satellite);

class CpfAcquisition {
public:
    explicit CpfAcquisition(std::filesystem::path cache_directory);

    // Returns the newest current EDC CPF, downloading it only when absent. Both
    // downloaded and cached files are parsed and their headers are validated
    // before a path is returned.
    [[nodiscard]] std::filesystem::path acquire(const Satellite& satellite) const;

private:
    // Cache is deliberately local and reproducible: a downloaded file is
    // retained by its provider-issued filename rather than overwritten.
    std::filesystem::path cache_directory_;
    // All satellites share the same EDC directory listing during one CLI run.
    mutable std::optional<std::string> current_listing_;
};

}  // namespace arctra

#endif
