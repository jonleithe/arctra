#ifndef ARCTRA_SATELLITE_FILE_H
#define ARCTRA_SATELLITE_FILE_H

#include "arctra/satellite.h"

#include <filesystem>
#include <vector>

namespace arctra {

// Append satellites named in a plain-text selection file. Each non-empty,
// non-comment line contains one name accepted by SatelliteCatalog::find(); a
// line whose first non-whitespace character is '#' is a comment.
//
// Throws an error naming the file and line if it cannot read the file or an
// entry is not a known satellite.
void append_satellites_from_file(std::vector<Satellite>& result,
                                 const SatelliteCatalog& catalog,
                                 const std::filesystem::path& path);

}  // namespace arctra

#endif
