#ifndef ARCTRA_CPF_ACQUISITION_H
#define ARCTRA_CPF_ACQUISITION_H

#include "arctra/satellite.h"

#include <filesystem>
#include <optional>
#include <string>

namespace arctra {

class CpfAcquisition {
public:
    explicit CpfAcquisition(std::filesystem::path cache_directory);

    // Returns the newest current EDC CPF, downloading it only when absent.
    [[nodiscard]] std::filesystem::path acquire(const Satellite& satellite) const;

private:
    std::filesystem::path cache_directory_;
    mutable std::optional<std::string> current_listing_;
};

}  // namespace arctra

#endif
