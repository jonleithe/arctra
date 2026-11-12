#ifndef ARCTRA_PASS_FINDER_H
#define ARCTRA_PASS_FINDER_H

#include "arctra/cpf.h"
#include "arctra/observatory.h"
#include "arctra/prediction.h"
#include "arctra/satellite.h"

namespace arctra {

[[nodiscard]] PredictionTable find_passes(
    const CpfEphemeris& ephemeris,
    const Satellite& satellite,
    const Observatory& observatory,
    const PredictionInterval& interval);

}  // namespace arctra

#endif
