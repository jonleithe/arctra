#ifndef ARCTRA_GLOBE_LAUNCHER_H
#define ARCTRA_GLOBE_LAUNCHER_H

namespace arctra {

// Replaces the current process with the loopback-only browser host. The
// authoritative prediction work remains in arctra-globe-worker.
[[nodiscard]] int run_globe();

}  // namespace arctra

#endif
