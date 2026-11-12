#ifndef ARCTRA_CPF_H
#define ARCTRA_CPF_H

#include "arctra/ephemeris.h"

#include <filesystem>
#include <istream>
#include <string>
#include <vector>

namespace arctra {

struct CpfHeader {
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
    UtcTime epoch;
    int leap_second_flag;
    EarthFixedPosition position;
};

class CpfEphemeris {
public:
    CpfEphemeris(CpfHeader header, std::vector<CpfPositionRecord> positions);

    [[nodiscard]] const CpfHeader& header() const noexcept;
    [[nodiscard]] const std::vector<CpfPositionRecord>& positions() const noexcept;
    [[nodiscard]] EarthFixedPosition interpolate(UtcTime epoch) const;

private:
    CpfHeader header_;
    std::vector<CpfPositionRecord> positions_;
};

[[nodiscard]] CpfEphemeris parse_cpf(std::istream& input);
[[nodiscard]] CpfEphemeris load_cpf(const std::filesystem::path& path);

}  // namespace arctra

#endif
