#ifndef ARCTRA_PASS_FINDER_H
#define ARCTRA_PASS_FINDER_H

#include "arctra/cpf.h"
#include "arctra/observatory.h"
#include "arctra/prediction.h"
#include "arctra/satellite.h"

namespace arctra {

// Find all portions of the requested interval at or above the observatory's
// elevation threshold. Results retain partial passes at interval boundaries.
// The ephemeris must cover the complete interval; no extrapolation is done.
[[nodiscard]] PredictionTable find_passes(
    const CpfEphemeris& ephemeris,
    const Satellite& satellite,
    const Observatory& observatory,
    const PredictionInterval& interval);

}  // namespace arctra

#endif
