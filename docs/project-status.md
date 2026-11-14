# Arctra project status

Last updated: 2026-11-14

This is a short handoff for a developer or Codex session joining the project.
The authoritative implementation is the current `master` branch; begin with
`git status`, `README.md`, and the tests before changing behavior.

## What works now

- C++20/CMake command-line application for geometric SLR pass prediction.
- CPF v2 parser for passive Earth-orbiting targets in geocentric true
  body-fixed frame `0`, using type-10 direction-0 Cartesian position records.
- Ten-point, ninth-order Lagrange interpolation, WGS 84 station conversion,
  and ECEF-to-ENU azimuth, elevation, and slant range.
- Pass search above the observatory's configured minimum elevation, with
  bisection-refined threshold crossings and golden-section maximum elevation.
- EDC CPF acquisition with local cache, fallback to valid cached files when
  EDC is unavailable, and header identity checks for target name, ILRS ID, and
  NORAD ID.
- Checked-in EDC active-target catalog (`data/edc-active-satellites.csv`), CLI
  list/search, repeatable target files, and a terminal selector.
- Live tracker: `arctra track --observatory nyal.ini`. The catalog and live
  data stay in one terminal view. Space selects/deselects targets; one CPF load
  runs in the background at a time and appears as loading/queued/failed in the
  live pane. Ready rows are ordered: above threshold first, then by time to
  next rise. `--pri` preselects targets from the editable checked-in
  `priorities.sat` file (resolved from the current directory) and queues them
  after the dashboard opens.
- Tracker azimuth/elevation display uses four decimal places; light time is
  round-trip microseconds (`us (RT)`). Space clears the search after toggling.
- Countdown searches return the first rise/set, stopping at actual position
  coverage or header intent, whichever ends first. Regression tests cover
  shortened coverage, no-event results, and a later data gap.
- Initial NASA RATGUI photo comparison is recorded in
  `docs/ratgui-comparison-2026-11-14.md`; both displays use UTC. This is
  preliminary evidence, not completed independent accuracy validation.

## Important conventions and limits

- All output is instantaneous **geometric** CPF/ECEF-to-ENU information. It is
  not operational laser pointing or control.
- The tracker displays vacuum round-trip light time `2 * range / c`, labelled
  `us (RT)`, not an iterated SLR light-time or range correction.
- A valid current CPF is necessary but does not by itself guarantee that a
  target will be visible from the selected station.
- Lunar reflectors (Apollo, Luna, NGLR) remain in the local catalog but are not
  supported by the current Earth-orbit geometry path.
- CPF time labels are stored in a POSIX-style `sys_time`; the implementation
  must not be relied upon across a leap second.
- Current cache behavior is intentionally basic. It has no retry strategy and
  does not yet persist full source/retrieval provenance metadata.
- Countdown scanning can miss a complete visibility interval between its
  30-second samples. No-event results and failures remain cached until the
  target is reselected. The requested `no rise within CPF coverage` message
  currently also covers continuously above-threshold targets.
- No SGP4/OMM propagation path exists yet.

## Build and test

```bash
./build.sh debug
ctest --preset dev
```

The `dev` preset builds in `build/dev`; `release` builds in `build/release`.

## Suggested next work

1. Follow up the initial RATGUI photos with exact-epoch comparisons of several
   CPF-derived passes against Ny-Ålesund operational predictions.
   Record exact UTC epochs, observatory configuration, CPF filename/provider,
   azimuth, elevation, range, and AOS/LOS differences. This validation matters
   more than adding another propagation source.
2. Review live-tracker interaction and terminal layout at the station; retain
   the current background-loader and no-extrapolation safeguards.
3. Add cache provenance/retries only after deciding the desired offline and
   refresh policy.
4. Consider categorical mission metadata for the local catalog separately from
   CPF availability. Do not infer scientific/operational suitability from a
   name alone.

See `docs/software-flow.md` for the algorithm/data paths and `AGENTS.md` for
development constraints and the longer roadmap.
