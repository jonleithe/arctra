#ifndef ARCTRA_OBSERVATORY_FILE_H
#define ARCTRA_OBSERVATORY_FILE_H

#include "arctra/observatory.h"
#include <filesystem>

namespace arctra {
[[nodiscard]] Observatory load_observatory_ini(const std::filesystem::path& path);
}

#endif
