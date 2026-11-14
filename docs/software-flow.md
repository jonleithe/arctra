# Arctra software flow

This document describes the current `arctra predict` and `arctra track` paths.
All reported directions and ranges are instantaneous geometric quantities; this
is not telescope or laser-control software.

```mermaid
flowchart TD
    A[User runs arctra predict] --> B[Parse CLI options\n--satellite, --observatory, --dur]
    B --> C[Load observatory INI\nlatitude, longitude, ellipsoidal height, minimum elevation]
    B --> D[Set interval\nUTC now to now + duration]
    C --> E[Convert geodetic station position\nto WGS 84 ECEF]

    D --> F{For each selected satellite}
    F --> G[Inspect EDC current CPF listing]
    G --> H{Newest CPF already cached?}
    H -- yes --> I[Parse cached CPF]
    H -- no --> J[Download CPF to a temporary cache file]
    J --> K[Parse temporary CPF]
    G -- EDC unavailable --> L[Use newest cached CPF]
    L --> I

    I --> M{CPF valid for\nrequested satellite?}
    K --> M
    M -- no --> N[Stop with actionable error\nDo not retain downloaded file]
    M -- yes --> O{Downloaded this run?}
    O -- yes --> P[Store verified download in cache]
    O -- no --> Q[Check CPF covers full\nrequested interval]
    P --> Q

    Q -- no --> R[Stop: no extrapolation]
    Q -- yes --> S[Evaluate look angles]
    E --> S

    S --> T[For each evaluation epoch:\nselect ten CPF position records\nand ninth-order Lagrange interpolate]
    T --> U{Adequate records and\nno data gap?}
    U -- no --> V[Stop with interpolation error]
    U -- yes --> W[Satellite ECEF minus station ECEF]
    W --> X[Rotate vector ECEF to ENU]
    X --> Y[Calculate azimuth, elevation,\nand slant range]

    Y --> Z[Scan elevation every 30 seconds]
    Z --> AA[Refine threshold crossings\nby bisection]
    AA --> AB[Refine maximum elevation\nby golden-section search]
    AB --> AC[Collect AOS, maximum, LOS\nand maximum range/elevation]
    AC --> AD[Sort all satellite passes\nby culmination time]
    AD --> AE[Print UTC pass table]
```

## CPF acceptance checks

Before a CPF can be used, Arctra parses it and confirms that it is CPF v2 and
that its header target name, ILRS ID, and NORAD ID match the selected satellite.
The parser currently supports only passive Earth-orbiting targets in CPF
reference frame `0`: geocentric true body-fixed. It rejects other target types
and frames rather than mixing incompatible coordinates.

## Live tracker flow

`arctra track --observatory FILE` keeps the local catalog and selected target
data in one terminal view. The catalog is checked in with the application;
typing/searching in this view does not perform an online satellite lookup.

```mermaid
flowchart TD
    A[User runs arctra track] --> B[Load local catalog and observatory INI]
    B --> C[Render catalog pane and live-data pane]
    C --> D{User presses Space}
    D -- deselect --> E[Remove target from live evaluation]
    D -- select Earth-orbiting target --> F[Queue CPF acquisition]
    D -- select catalog-only lunar target --> G[Show unsupported-target message]

    F --> H[One background CPF load at a time]
    H --> I[Acquire cached or current EDC CPF]
    I --> J{Header and supported geometry valid?}
    J -- no --> K[Show CPF failed; user may retry]
    J -- yes --> L[Add target to live evaluation]
    L --> C

    C --> M[Each second: interpolate CPF at UTC now]
    M --> N[Convert ECEF position to ENU look angle]
    N --> O[Display azimuth, elevation, range and range/c]
    O --> P{Above minimum elevation?}
    P -- yes --> Q[Show ABOVE and countdown to predicted set]
    P -- no --> R[Show below and countdown to predicted rise]
    Q --> S[Order live rows: above first]
    R --> S
    S --> C
```

The next threshold event is found with the same CPF pass-search/refinement
logic as `predict`, then cached until it occurs. The display refresh is live;
the ephemeris is the downloaded CPF, not real-time spacecraft telemetry.

## Deliberate limits

- A CPF must cover the full requested interval; Arctra does not extrapolate.
- Station conversion uses WGS 84. The supplied station coordinates must be
  appropriate for the CPF terrestrial frame and epoch for precision work.
- The output omits light-time iteration, atmosphere/refraction, station
  displacement, instrument calibration, safety interlocks, and any control
  interface.
- Live-track microseconds are geometric vacuum range divided by the speed of
  light. They are not corrected one-way or two-way SLR timing.
- The current UTC representation cannot preserve a distinct leap-second label;
  do not use this implementation to predict across a leap second.
