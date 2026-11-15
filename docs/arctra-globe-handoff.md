# Arctra Globe — project handoff

## Purpose

Create a separate visual companion application for Arctra: an interactive,
Google-Earth-like globe showing CPF-predicted SLR satellites, their future
trajectories, observer sites, and geometric visibility overlays.

This is deliberately **not** a feature to grow inside the `arctra` CLI
repository. Arctra remains the transparent C++ CPF/acquisition/interpolation
and geometric-prediction tool. The globe application consumes data it exports.

The user’s reference was an ILRS-style Earth view with labelled satellites and
coloured tracks. Desired interaction goes beyond that static view: mouse
rotate/pan/zoom, live predicted positions, selectable targets, and arbitrary
observer locations.

## Recommended technical direction

Use **CesiumJS** for the first implementation.

- It already provides an interactive WGS 84 globe, camera controls, labels,
  moving entities, and polylines.
- Keep all CPF parsing, interpolation, ECEF/ENU geometry, acquisition, and
  validity policies in C++/Arctra. Do not reimplement the orbital mathematics
  in JavaScript.
- Start with a static, versioned JSON prediction snapshot written by Arctra or
  derived from a cached CPF. Add a local HTTP/WebSocket feed only after the
  static globe is useful.

A native Qt Quick application is a viable future alternative, but requires
building the globe rendering and interaction layer ourselves. CesiumJS is the
faster route to the desired 3D-globe experience.

## Strict limits

- The application displays **CPF-based geometric predictions**, not live
  telemetry and not telescope/laser-control data.
- Do not describe it as an operational pointing or ranging solution.
- It must preserve Arctra’s no-extrapolation policy: no position, trajectory,
  or event outside valid CPF coverage.
- Current support is passive Earth-orbiting CPF targets in supported frame-0
  files. Lunar reflectors are not supported by the Earth-orbit path.
- A geometric visualisation has no terrain/obstruction mask, atmospheric
  refraction, light-time iteration, station displacement, or instrumentation
  correction unless those capabilities are explicitly added and documented.

## Data boundary

Prefer an explicit versioned interchange format over sharing C++ source files.

Example conceptual snapshot:

```json
{
  "schema_version": 1,
  "generated_utc": "2026-11-15T12:00:00Z",
  "prediction_kind": "instantaneous geometric CPF/ECEF",
  "observatories": [
    {
      "id": "nyal",
      "name": "Ny-Alesund",
      "latitude_deg": 78.923,
      "longitude_deg": 11.923,
      "ellipsoidal_height_m": 50.0,
      "minimum_elevation_deg": 10.0
    }
  ],
  "targets": [
    {
      "key": "lageos1",
      "name": "LAGEOS-1",
      "cpf": {
        "coverage_start_utc": "...",
        "coverage_end_utc": "...",
        "provider": "EDC"
      },
      "current": {
        "epoch_utc": "...",
        "ecef_m": [0.0, 0.0, 0.0],
        "azimuth_deg": 0.0,
        "elevation_deg": 0.0,
        "slant_range_m": 0.0,
        "above_minimum_elevation": false
      },
      "trajectory": [
        {"epoch_utc": "...", "ecef_m": [0.0, 0.0, 0.0]}
      ]
    }
  ]
}
```

The schema must record the source/provider and CPF coverage. It should carry
only supported, valid samples; never invent a track after coverage ends.

## Globe scene

The first useful scene should include:

1. WGS 84 Earth globe with normal mouse rotate/pan/zoom.
2. One or more observer pins, beginning with Ny-Ålesund.
3. Satellite marker at the CPF-predicted ECEF position and actual displayed
   altitude above the ellipsoid.
4. Label with target name and compact prediction health/visibility state.
5. A sampled future/past CPF trajectory. Draw the actual sampled spatial
   ECEF path as a 3D polyline; do not substitute an invented Keplerian orbit.
6. Selection panel for exact current geometric azimuth, elevation, range,
   CPF expiry, and AOS/LOS where available.

Provide a distinct toggle for a **ground track** (the satellite subpoint) and
the **3D spatial trajectory**. They are different visualisations and should
not be conflated.

## Horizon / visibility overlays

The requested “horizon circle” is valuable for intuition but must be presented
correctly.

- The authoritative visibility result is Arctra’s existing exact geometric
  ECEF-to-ENU elevation calculation at the station.
- A fixed circle around a station is only a planning aid because the surface
  radius associated with a minimum-elevation threshold depends on satellite
  altitude (and an ellipsoidal Earth is not a perfect sphere).
- A better optional overlay is the selected satellite’s instantaneous
  visibility footprint/cap for the observer’s configured minimum elevation.
  It changes with the satellite altitude. Its visual relation to the observer
  is explanatory; it does not replace the exact ENU result.
- An arbitrary clicked globe position may create a **temporary geometric
  observer** with explicit assumed ellipsoidal height and minimum elevation.
  It must not be labelled as a surveyed/operational station.

## Suggested milestones

### Milestone A — offline visual proof

- Load one checked-in JSON snapshot for Ny-Ålesund and one or more satellites.
- Render globe, satellite positions, labels, and 30–60 minutes of sampled,
  valid CPF trajectory.
- Implement target selection and camera navigation.
- No downloader, WebSocket, or arbitrary-location calculation yet.

### Milestone B — data export contract

- Add a narrowly scoped Arctra export mode or standalone exporter that writes
  the documented snapshot schema from a local cached CPF.
- Test schema generation against fixture CPF data; preserve source/coverage.
- Do not make the GUI parse CPF directly in this milestone.

### Milestone C — observer and visibility UX

- Show the configured station and selected-target current az/el/range.
- Add configurable minimum elevation and clearly labelled visual overlays.
- Add a temporary clicked observer with non-survey-grade warning.

### Milestone D — live local updates

- Run a local-only service or WebSocket from the C++ side.
- Update snapshot/positions approximately once per second without blocking the
  rendering thread.
- Reuse Arctra’s CPF refresh, expiry, validation, and no-extrapolation rules.

## Engineering guidelines

- Keep the C++ prediction engine authoritative; the browser/UI only renders
  supplied state and requests user choices.
- Keep geometry units explicit: ECEF metres, range metres, degrees only at UI
  boundaries, ISO-8601 UTC timestamps.
- Retain provider/file provenance in UI diagnostics.
- A globe library’s Earth-fixed coordinate convention must be compared with
  the supported CPF frame before asserting precision. A visually plausible
  placement is not independent validation.
- Test data contracts, trajectory coverage boundaries, and the exact
  above-threshold state separately from rendering tests.

## Reference links

- CesiumJS fundamentals: https://cesium.com/learn/cesiumjs-fundamentals/
- CesiumJS entities and polylines:
  https://cesium.com/learn/cesiumjs-learn/cesiumjs-creating-entities/
- Arctra project status and mathematical/operational limits should be copied
  from the source repository at project creation time.
