#ifndef ARCTRA_CPF_H
#define ARCTRA_CPF_H

#include "arctra/ephemeris.h"

#include <filesystem>
#include <istream>
#include <string>
#include <vector>

namespace arctra {

struct CpfHeader {
    // Values from CPF H1 and H2. Times are the H2 intended-use interval, not
    // necessarily the epochs of the first and last type-10 records.
    int format_version;
    std::string ephemeris_source;
    std::string target_name;
    int ilrs_satellite_id;
    int sic;
    int norad_catalog_id;
    UtcTime intended_start;
    UtcTime intended_end;
    int nominal_step_seconds;
    int reference_frame;
};

struct CpfPositionRecord {
    // Type-10, direction-0 common-epoch position. Coordinates are ECEF metres
    // for the currently supported CPF reference frame 0.
    UtcTime epoch;
    int leap_second_flag;
    EarthFixedPosition position;
};

class CpfEphemeris {
public:
    CpfEphemeris(CpfHeader header, std::vector<CpfPositionRecord> positions);

    [[nodiscard]] const CpfHeader& header() const noexcept;
    [[nodiscard]] const std::vector<CpfPositionRecord>& positions() const noexcept;
    // Evaluate position using CPF's 10-point, ninth-order Lagrange baseline.
    // Throws outside intended coverage, at a data boundary without a complete
    // interpolation window, or across a suspicious record gap.
    [[nodiscard]] EarthFixedPosition interpolate(UtcTime epoch) const;

private:
    CpfHeader header_;
    std::vector<CpfPositionRecord> positions_;
};

// Parse the supported CPF-v2 subset. Unsupported frames and target classes are
// rejected rather than transformed or silently misinterpreted.
[[nodiscard]] CpfEphemeris parse_cpf(std::istream& input);
[[nodiscard]] CpfEphemeris load_cpf(const std::filesystem::path& path);

}  // namespace arctra

#endif
