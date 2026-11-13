# Arctra software flow

This chart describes the current `arctra predict` path: from a command-line
request to geometric pass predictions. All reported directions and ranges are
instantaneous geometric quantities; this is not telescope or laser-control
software.

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

## Deliberate limits

- A CPF must cover the full requested interval; Arctra does not extrapolate.
- Station conversion uses WGS 84. The supplied station coordinates must be
  appropriate for the CPF terrestrial frame and epoch for precision work.
- The output omits light-time iteration, atmosphere/refraction, station
  displacement, instrument calibration, safety interlocks, and any control
  interface.
