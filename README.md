# Arctra

Arctra is an educational C++20 command-line predictor for geometric Satellite
Laser Ranging passes. It downloads ILRS CPF v2 predictions from the EUROLAS
Data Center (EDC), interpolates Earth-fixed positions, and calculates passes for
a configured observatory.

It is not certified telescope or laser-control software. Results do not include
light-time iteration, atmospheric refraction, station displacement,
instrumentation, or operational safety constraints.

## Build

Required tools are CMake 3.20 or newer, Ninja, a C++20 compiler, and libcurl.

```bash
./build.sh debug
./build.sh release
./build.sh all
```

Run the tests with:

```bash
ctest --preset dev
```

## Find satellites

Arctra loads the complete checked-in EDC active-satellite snapshot at run time.
List it or search by partial CPF key/name, NORAD catalog ID, or ILRS satellite ID:

```bash
./build/release/arctra satellites list
./build/release/arctra satellites search lageos
./build/release/arctra satellites search 8820
```

Entries marked `CPF candidate` have the identifiers needed to attempt CPF
acquisition; this is not a guarantee that a current compatible file exists.
Entries marked `catalog only` (currently lunar reflectors without a NORAD ID)
can be discovered but cannot be passed to the Earth-orbiting CPF predictor.
Search and selection are separate from prediction so a future interactive TUI
can reuse the same catalog without changing the orbital calculations.

## Predict passes

For interactive terminal selection, use `--select`. Typing moves the highlighted
row to a matching satellite without hiding the full catalog (substring matching
with a fuzzy fallback). Move with the arrow or page keys, toggle satellites with
Space, use Home/End for the beginning or end of the catalog, press Enter to run
the prediction, or Escape to cancel. Selections remain visible in a separate pane.

```bash
./build/release/arctra predict --select --observatory nyal.ini --dur 24h
```

```bash
./build/release/arctra predict \
  --satellite lageos1,lageos2,etalon1 \
  --observatory nyal.ini \
  --dur 24h
```

`--dur` defaults to 24 hours. Satellite names are case-insensitive and may be
separated by commas. Current CPF files are cached below `.arctra-cache/cpf/`.
If EDC is temporarily unavailable, an existing cached file may be used, but it
must still cover the complete requested interval.

For a repeatable target selection, use `--sat-file FILE`. The file contains one
currently supported satellite key or display name per line; blank lines and
lines beginning with `#` are ignored. It may be combined with `--satellite`.

```text
# targets.txt
lageos1
LARES-2
etalon1
```

```bash
./build/release/arctra predict --sat-file targets.txt --observatory nyal.ini --dur 24h
```

An example selection is provided in `examples/standard-slr-targets.txt`.

The machine-readable snapshot is
[`data/edc-active-satellites.csv`](data/edc-active-satellites.csv), with source
notes and a readable table in
[`docs/edc-active-satellite-catalog.md`](docs/edc-active-satellite-catalog.md).
Prediction still verifies that a current compatible CPF exists for a selected
target; catalog membership alone does not guarantee prediction availability.
Targets without a current EDC CPF are listed and skipped automatically so the
remaining selected satellites can still be predicted.

The observatory file uses decimal degrees and ellipsoidal height in metres:

```ini
[observatory]
name = Ny-Alesund
lat = 78.923
lon = 11.923
height = 50.0
min-elevation = 10.0
```

Replace the example coordinates with surveyed coordinates and document their
reference-frame realization and coordinate epoch before precision use.

## Current mathematical path

Arctra currently supports passive Earth-orbiting targets in CPF v2 reference
frame 0 (geocentric true body-fixed). It uses ten-point, ninth-order Lagrange
interpolation, the WGS 84 ellipsoid for station conversion, geometric ECEF-to-ENU
look angles, 30-second pass scanning, bisection for threshold crossings, and a
golden-section search for maximum elevation. It does not extrapolate beyond CPF
coverage.
