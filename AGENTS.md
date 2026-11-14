# AGENTS.md — SLR Satellite Pass Predictor

## Purpose

Build a small, accurate, testable command-line program to predict when selected Satellite Laser Ranging (SLR) targets pass above a specified observing station, and calculate their topocentric azimuth, elevation, and slant range at requested epochs.

This is initially an **educational and engineering analysis tool**, not certified software for controlling an SLR telescope or laser. The developer wants to understand and implement the mathematics, rather than hiding it behind an opaque library.

## Primary users and development preferences

- The principal developer is an engineer working with space geodesy and newly started SLR operations in Ny-Ålesund, Svalbard.
- **Prefer modern C++ (C++20)** and a straightforward command-line interface. Python is acceptable for numerical cross-checks, test-data generation, or prototypes, but should not replace the C++ implementation without discussion.
- Develop in VS Code with Codex; target Linux (including Arch Linux).
- Prefer small modules, transparent calculations, explicit units, reproducible tests, meaningful diagnostics, and readable code over framework-heavy designs.
- Explain important orbital mechanics, interpolation, time-scale, and coordinate-frame decisions. Do not silently outsource the mathematical core to a library when the goal is to learn it.
- Do not assume the developer wants all features implemented at once. Implement incremental, verifiable milestones.

## Initial target satellites

| Satellite | NORAD catalog ID | ILRS satellite ID |
| --- | ---: | ---: |
| LAGEOS-1 | 8820 | 7603901 |
| LAGEOS-2 | 22195 | 9207002 |
| ETALON-1 | 19751 | 8900103 |
| ETALON-2 | 20026 | 8903903 |
| LARES | 38077 | 1200601 |
| LARES-2 | 53105 | 2208001 |

The six satellites above are the initial scope, but do not hard-code the algorithms to only these objects. Keep satellite selection/configuration extensible.

## Data sources and reference material

### Preferred for SLR: ILRS Consolidated Prediction Format (CPF)

- ILRS format and documentation: https://ilrs.gsfc.nasa.gov/data_and_products/formats/cpf.html
- ILRS predictions overview: https://ilrs.gsfc.nasa.gov/data_and_products/predictions/index.html
- NASA CDDIS archive: https://cddis.nasa.gov/archive/slr/cpf_predicts_v2/
- EUROLAS Data Center (EDC): https://edc.dgfi.tum.de/
- EDC API documentation: https://edc-api.readthedocs.io/

CPF supplies predicted satellite Cartesian states at tabulated epochs; interpolate those data to calculate positions at arbitrary instants. Start by implementing and validating **CPF v2** parsing and interpolation. Consult the actual format specification for record definitions, reference frame, time scale, interpolation rules, and optional records. Do **not** rely on illustrative snippets as a format specification. Verify live download endpoints, authentication needs, and API version before writing a downloader; NASA CDDIS may require Earthdata authentication.

### Alternative/independent approach: CelesTrak GP/OMM + SGP4

- CelesTrak data/query format: https://celestrak.org/NORAD/documentation/gp-data-formats.php
- Geodetic group (JSON): https://celestrak.org/NORAD/elements/gp.php?GROUP=geodetic&FORMAT=JSON
- Individual satellite example: https://celestrak.org/NORAD/elements/gp.php?CATNR=8820&FORMAT=JSON
- Vallado SGP4 reference and test cases: https://celestrak.org/publications/AIAA/2006-6753/

OMM/GP elements with SGP4 are appropriate for exploring orbit propagation and comparing general pass predictions, but **are not a substitute for SLR-grade CPF predictions**. SGP4 commonly produces a TEME state; that must not be mistaken for an ITRF/Earth-fixed position.

### Other useful reference

- JPL Horizons API: https://ssd-api.jpl.nasa.gov/doc/horizons.html — useful for selected spacecraft/solar-system ephemerides, **not** the primary source for these six SLR targets.

## Proposed work sequence

## Current implementation status — 2026-11-14

The repository has progressed beyond the initial Milestone 1-only brief. Do
not treat the milestones below as an indication that earlier work is absent;
inspect the current code and tests before proposing a duplicate implementation.

Implemented and committed on `master`:

- CPF v2 parsing for the currently supported subset: passive, Earth-orbiting
  targets in geocentric true body-fixed frame `0`, using common-epoch type-10
  position records.
- Ten-point, ninth-order Lagrange interpolation; geometric WGS 84 ECEF-to-ENU
  azimuth, elevation, and slant range; threshold pass finding with refined
  AOS/LOS and culmination.
- EDC current-directory acquisition with provider-issued-file caching and CPF
  header validation against the selected satellite.
- A checked-in 144-entry EDC active-target catalog, name/ID search, target
  files, and interactive terminal selection.
- `arctra track --observatory FILE`: a live terminal dashboard with the local
  catalog on the left and selected target data on the right. Targets can be
  added/removed in place; CPF loading is serialized in the background and is
  represented in the live pane. Ready targets are ordered with currently
  above-threshold targets first, then by time to next rise.
- Unit and CLI smoke tests. Run `ctest --preset dev` after a development build.

Current limitations that must remain visible in user-facing work:

- The tracker is based on a downloaded CPF and instantaneous geometric
  quantities. It is neither live telemetry nor telescope/laser-control
  software.
- The displayed one-way and round-trip microseconds are `range / c` and
  `2 * range / c` in vacuum, not a corrected SLR light-time solution.
- Lunar reflectors remain catalog-visible but unsupported by the Earth-orbit
  path. Their CPF use requires transmit/return light-time legs and lunar
  orientation treatment.
- The current UTC representation cannot preserve a distinct `23:59:60` label;
  do not claim reliable prediction across a leap second.
- There is no independent operational comparison yet. This is the highest
  value next validation task when station predictions are available.
- Acquisition has a basic file cache but not yet retry policy or complete cache
  provenance metadata. SGP4/OMM support is not implemented.

See `docs/project-status.md` for a concise developer handoff and
`docs/software-flow.md` for the predict and live-tracker flows.

### Milestone 1 — CPF geometry for one satellite (implemented subset; validation pending)

1. Obtain a real, recent CPF v2 prediction for LAGEOS-1, initially by manual download if that is simpler.
2. Parse the header and required position records; reject malformed or unsupported records with actionable errors.
3. Represent epochs unambiguously, with documented handling of UTC, leap seconds, and any time scale required by CPF.
4. Interpolate the Cartesian satellite position at a requested epoch. Implement the CPF specification's recommended method (historically 10-point / ninth-order Lagrange interpolation for position); **confirm requirements against the current specification**. Handle interval boundaries and gaps carefully; do not extrapolate without an explicit policy.
5. Read the station's surveyed coordinates, including reference-frame realization and coordinate epoch where relevant. For a first demonstration, approximate Ny-Ålesund coordinates may be supplied explicitly, but never label them surveying-grade.
6. Ensure satellite and station positions refer to compatible frames and epochs. If CPF contains Earth-fixed coordinates, confirm the precise meaning of the frame flag instead of assuming all files use identical conventions.
7. Compute topocentric East-North-Up (ENU) position, geometric azimuth, elevation, and slant range.
8. Compare output at several epochs with a trusted independent prediction or reference interpolation implementation.

### Milestone 2 — Pass search for all six satellites (implemented for supported CPF targets)

1. Load predictions for all six target satellites.
2. Accept observer station, UTC start/end timestamps, and configurable minimum elevation angle.
3. Find rise/entry and set/exit events relative to the **minimum-elevation threshold**; use a coarse scan followed by root refinement (e.g., bisection or Brent's method).
4. Compute maximum elevation and its epoch, refining the peak rather than accepting only the coarse-grid maximum.
5. Print UTC AOS, culmination/max-elevation time, LOS, duration, maximum elevation, and optional azimuth/range at important epochs.
6. Correctly handle partial passes intersecting interval boundaries and continuous above-threshold visibility; don't mislabel these as ordinary AOS/LOS events.

### Milestone 3 — Download and refresh data (basic acquisition/cache implemented)

1. Add an HTTP(S) retrieval layer, preferably libcurl, with retries and clear error messages.
2. Implement a cache with source URL, retrieval timestamp, prediction interval, target ID, and format/version metadata.
3. Keep the parser usable on local files for offline work and reproducible testing.
4. Verify that the prediction covers each requested epoch; detect stale, missing, or mismatched data instead of silently proceeding.

### Milestone 4 — Independent SGP4 path (not started; optional after CPF validation)

1. Download OMM/GP data for the geodetic satellites from CelesTrak.
2. Use a well-validated SGP4 implementation or implement it carefully with Vallado's reference tests.
3. Convert TEME outputs to a compatible Earth-fixed frame, accounting for the required Earth-orientation/time conventions.
4. Run the same topocentric and pass-search calculations through a common interface.
5. Compare CPF and SGP4 predictions, documenting that their accuracy and underlying models differ.

## Mathematical conventions

For a satellite position `r_sat` and station position `r_station` in a **common Earth-fixed Cartesian frame**, define

`rho = r_sat - r_station`.

For station geodetic latitude `phi` and longitude `lambda` (radians), the ECEF-to-ENU rotation of `rho = (dx, dy, dz)` is:

```text
E = -sin(lambda)*dx + cos(lambda)*dy
N = -sin(phi)*cos(lambda)*dx - sin(phi)*sin(lambda)*dy + cos(phi)*dz
U =  cos(phi)*cos(lambda)*dx + cos(phi)*sin(lambda)*dy + sin(phi)*dz

range     = sqrt(E*E + N*N + U*U)
azimuth   = atan2(E, N)  [normalize into 0..360 degrees]
elevation = atan2(U, sqrt(E*E + N*N))
```

Use **geodetic**, not geocentric, latitude for the local ENU frame. Distinguish degrees from radians and metres from kilometres in interfaces and tests. State the ellipsoid used where geographic coordinates are converted into Cartesian coordinates.

These equations describe **instantaneous geometric directions**, not fully corrected laser ranging or telescope pointing. Do not present them as operational SLR pointing solutions. Precision work requires further treatment of light time, satellite motion during transit, atmosphere, instrumentation, station displacement, and operational safety constraints.

## Engineering and quality requirements

- Build with CMake. Favor standard-library functionality; consider libcurl for HTTP and a small JSON parser for OMM when needed.
- Keep separate modules for CPF parsing/interpolation, coordinate/reference-frame transformations, pass finding, data acquisition, and CLI presentation.
- Maintain source provenance and explicit assumptions; use typed domain concepts or unit-safe naming where helpful.
- Supply unit tests and fixture files. Include interpolation tests at tabulated epochs, known coordinate-transform cases, angular normalization, timestamp boundaries, and threshold-crossing cases.
- Compare CPF-derived positions or azimuth/elevation against an **independent** trustworthy implementation or operational prediction, not merely against the same formulas implemented twice.
- Avoid invented ephemerides or pass times. If the required data are unavailable, report that instead of producing plausible-looking results.
- Do not commit credentials, Earthdata tokens, or private station configuration.
- Avoid renaming unrelated files or performing substantial refactors without a clear benefit.

## Proposed project layout (flexible)

```text
slr-predict/
├── AGENTS.md
├── CMakeLists.txt
├── README.md
├── src/
│   ├── main.cpp
│   ├── cpf/
│   ├── coordinates/
│   ├── prediction/
│   └── acquisition/
├── include/
├── tests/
└── examples/
```

## Current handoff for Codex

Start by reading `docs/project-status.md`, `README.md`, and the current test
suite. Preserve the existing mathematical and operational limits. The next
recommended task is an evidence-backed comparison of CPF-derived output with
Ny-Ålesund operational predictions at several epochs; record the exact CPF
provider/file, station configuration, and comparison results. Do not add SGP4,
operational pointing/control, or lunar tracking merely because those future
paths are listed above.
