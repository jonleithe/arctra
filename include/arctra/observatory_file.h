#ifndef ARCTRA_OBSERVATORY_FILE_H
#define ARCTRA_OBSERVATORY_FILE_H

#include "arctra/observatory.h"
#include <filesystem>

namespace arctra {
// Read the small INI subset used by the CLI. Required [observatory] keys are
// name, lat, lon, height, and min-elevation; numeric values are decimal.
[[nodiscard]] Observatory load_observatory_ini(const std::filesystem::path& path);
}

#endif
