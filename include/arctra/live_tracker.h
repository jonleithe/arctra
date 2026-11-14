#ifndef ARCTRA_LIVE_TRACKER_H
#define ARCTRA_LIVE_TRACKER_H

#include "arctra/cpf.h"
#include "arctra/observatory.h"
#include "arctra/prediction.h"
#include "arctra/satellite.h"

#include <optional>
#include <vector>

namespace arctra {

// Instantaneous geometric tracking data. Light times are derived solely from
// the geometric slant range and the defined vacuum speed of light; they are
// not a two-way SLR light-time solution.
struct LiveTrackSample {
    Satellite satellite;
    LookAngle look;
    bool above_minimum_elevation;
    double one_way_light_time_us;
    double round_trip_light_time_us;
};

enum class VisibilityEventKind { rises_above_threshold, sets_below_threshold };

struct NextVisibilityEvent {
    VisibilityEventKind kind;
    UtcTime time;
};

// A selected target together with the CPF used to evaluate it. The dashboard
// owns these values so a downloaded file cannot change beneath a live run.
struct LiveCpfTarget {
    Satellite satellite;
    CpfEphemeris ephemeris;
};

[[nodiscard]] LiveTrackSample evaluate_live_track(
    const CpfEphemeris& ephemeris,
    const Satellite& satellite,
    const Observatory& observatory,
    UtcTime time);

// Finds the first crossing of the observatory's elevation threshold, scanning
// at 30-second intervals and refining by bisection. Search stops at the earlier
// of header intent and the final position record; interpolation errors propagate.
// A missing result means no crossing was found before coverage ends.
[[nodiscard]] std::optional<NextVisibilityEvent> next_visibility_event(
    const CpfEphemeris& ephemeris,
    const Satellite& satellite,
    const Observatory& observatory,
    UtcTime time);

// Refresh hourly, or 30 minutes before expiry; near expiry and after errors,
// retry after five minutes rather than queueing continuously.
[[nodiscard]] UtcTime next_cpf_refresh(UtcTime now, UtcTime coverage_end, bool successful);

// Linux terminal dashboard. The catalog stays visible throughout the run:
// Space adds/removes the highlighted target, fetching and validating its CPF
// when needed. It refreshes once per second and exits on q or Escape.
void run_live_tracker(const SatelliteCatalog& catalog,
                      std::vector<Satellite> initial_satellites,
                      const Observatory& observatory);

}  // namespace arctra

#endif
