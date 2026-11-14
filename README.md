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
`catalog only` does **not** mean that the target can never have a CPF: lunar
reflector CPFs use different transmit/return and lunar-orientation geometry,
which Arctra does not yet implement.
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

## Live tracker

The terminal dashboard samples the current CPF position once per second. Its
catalog remains visible throughout the session, so satellites can be selected
and deselected without leaving the live view:

```bash
./build/release/arctra track --observatory nyal.ini
```

Use `--pri` to open the tracker with the targets listed in the editable
checked-in [`priorities.sat`](priorities.sat) file already selected. The file
is resolved from the current directory; it uses the same one-key-per-line,
comments-allowed format as `--sat-file`. Their CPF requests are queued in the
background after the dashboard appears. `--pri` can be combined with
`--satellite` or `--sat-file` to add further initial selections.

```bash
./build/release/arctra track --pri --observatory nyal.ini
```

Use Space on the highlighted catalog target to start or stop tracking. Space
also clears the typed search while keeping the cursor on the current target.
The dashboard shows each selected target's instantaneous geometric azimuth and
elevation to four decimal places, slant range in kilometres, and whether
elevation is at or above the observatory's `min-elevation` threshold. Extra
displayed decimal places do not establish prediction accuracy. Angles use `°`;
range and round-trip time use spaces to group thousands (for example,
`20 345 km` and `135 729.8 μs (RT)`). The dashboard requires a UTF-8 terminal.

CPF retrieval for a newly selected target happens in the background. It appears
as `Loading CPF...` (or `Queued CPF...` when another request is already in
progress) in the live-data pane until its file has been validated.
It also reports round-trip light time (`2 * range / c`), labelled `μs (RT)`.
This is a vacuum light time derived from instantaneous CPF range, not a
corrected or operational SLR timing solution. Below the threshold it counts
down to the next predicted rise; once
above it counts down to the predicted set. Press `q` or Escape to leave the
dashboard.

The countdown scans forward in 30-second steps and refines the first threshold
crossing by bisection to a 10 ms bracket. It stops at the earlier of the CPF
header's intended end and the final position record; it never extrapolates.
A rise and set entirely between scan samples can be missed. A successful
search with no event displays `no rise within CPF coverage`; this means no
crossing was found within coverage, not that the satellite can never rise.
Currently the same message is also used for targets remaining above threshold.
`timer unavailable` means the search failed, for example at a data gap or an
unsupported interpolation epoch. Current look angles can remain available even
when a future countdown cannot be calculated. No-event results and failures
are cached until the CPF is refreshed; deselect/reselect to retry immediately.

Press **Tab** to switch focus between the catalog and live pane. The active
pane is marked `▶`; the live pane highlights its current target row rather
than its header. The catalog cursor is highlighted only when the catalog has
focus. Up/Down, Page Up/Down, and Home/End operate on the
focused pane. In the live pane they move the highlighted row and scroll independently of the catalog;
its header shows the visible range, for example `Live targets · 1–20 of 137`.
Queued and failed CPF loads are included in this range. Search and Space
selection operate only in the catalog. The footer shows the active controls.
Live rows continue to reorder as visibility changes; scrolling preserves the
row position rather than locking onto a particular satellite.

The tracker automatically checks EDC for each selected target once an hour,
or 30 minutes before its current usable coverage ends, whichever comes first.
Near expiry, and after failed checks, it retries at five-minute intervals.
These are application policies, not assumptions about provider update times.
Checks share the serialized background queue; a busy queue or slow network can
delay an individual check. Existing data stays available during refresh.
A replacement must pass target/header validation and interpolation at the current
UTC epoch before installation. Files that shorten existing coverage are retained
in the cache but do not replace the loaded prediction. Countdown caches reset
when a replacement is installed. Expired data is unavailable, never extrapolated.

The highlighted target's footer shows CPF health and the next check or refresh
status. With the live pane active, it also shows the exact usable expiry in UTC,
including for green `CPF OK` targets. Wide live rows also show expiry and pending/failed refresh status.
If EDC is unavailable, a usable cached file can support tracking, but the online
check is treated as failed and retried in five minutes. Failed initial loads
also retry automatically. Deselected targets stop receiving scheduled checks.

The live pane puts targets currently at or above the elevation threshold first,
then below-threshold targets by their next predicted rise. This is a display
order only; the catalog pane remains searchable in its normal catalog order.

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

## Validation status

The first station photo comparison with NASA RATGUI is recorded in
[the comparison notes](docs/ratgui-comparison-2026-11-14.md). Both clocks use
UTC. Preliminary angular agreement is encouraging, but the photographs are
not synchronized and do not establish operational accuracy. Exact-epoch
comparison with identified CPF inputs and station configuration remains open.

Visibility and prediction health have separate coloured labels: green `ABOVE`
and grey `below` refer to the configured minimum-elevation threshold. CPF status
is green `CPF OK` with no expiry countdown, yellow `CPF EXPIRING hh:mm:ss` within
30 minutes of usable expiry, and red `CPF EXPIRED` at expiry. Hours can exceed
24. Unavailable positions show `UNKNOWN` and omit current angles and range.
Status colour resets preserve the active row's reverse-video highlight.
