#ifndef ARCTRA_GLOBE_LAUNCHER_H
#define ARCTRA_GLOBE_LAUNCHER_H

#include "arctra/observatory.h"
#include "arctra/satellite.h"

#include <optional>
#include <vector>

namespace arctra {

// Replaces the current process with the loopback-only browser host. The
// authoritative prediction work remains in arctra-globe-worker.
[[nodiscard]] int run_globe(const std::optional<Observatory>& observatory,
                            const std::vector<Satellite>& initial_satellites,
                            bool override_saved_targets);

}  // namespace arctra

#endif
