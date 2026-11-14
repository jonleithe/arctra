# Initial RATGUI photo comparison — 2026-11-14

The developer supplied 15 station photographs in `Jottacloud.zip`, showing
NASA RATGUI and Arctra. The developer confirmed that RATGUI time is UTC;
Arctra explicitly labels its clock UTC. Photographs are not simultaneous.
Original images are external evidence and are not committed to this repository.

## Transcribed angular observations

NASA values below are from the **Telescope** row, not an uncorrected ephemeris
export. The GUI identifies RAT GUI v7.0.3-090826 and RAT'S NEST v7.0.1-081826.
All times are UTC on 2026-11-14. Arctra at capture displayed angles to 0.1°.

| Target | NASA image/time | NASA azimuth / elevation (°) | Arctra image/time | Arctra azimuth / elevation (°) |
| --- | --- | --- | --- | --- |
| LARES-2 | IMG_1365 / 10:44:09 | 121.0085 / 23.5693 | IMG_1366 / 10:44:13 | 120.9 / 23.6 |
| ETALON-1 | IMG_1373 / 11:01:27 | 299.8993 / 48.8313 | IMG_1374 / 11:01:31 | 299.9 / 48.8 |
| ETALON-1 | IMG_1376 / 11:04:21 | 298.4061 / 50.0295 | IMG_1375 / 11:04:17 | 298.4 / 50.0 |
| WESTPAC | IMG_1377 / 11:11:47 | 92.7560 / 37.1541 | IMG_1379 / 11:11:56 | 91.9 / 39.3 |

LARES-2 and ETALON-1 show encouraging preliminary angular agreement.
WESTPAC's difference needs evaluation at the same epoch. These are observations
at different times, not residuals or a quantitative accuracy assessment.
NASA's telescope/model corrections and display update timing are not established.

## Range and timing observations

RATGUI's Range field has no visible unit in these photos. Its magnitude suggests
round-trip microseconds, but this remains an inference requiring confirmation.
For ETALON-1, the Telescope Range field reads 136195.1 at 11:01:27 and
135719.8 at 11:04:21. Twice Arctra's displayed one-way microseconds gives
137783.4 at 11:01:31 and 135729.8 at 11:04:17, respectively. The first pair
has an unexplained discrepancy; do not interpret it as validated range agreement.

Arctra now displays `2 * range / c` as `us (RT)`, with four decimal places for
azimuth/elevation. This remains instantaneous geometric vacuum timing.

## Countdown defect reproduced separately

The photos show Jason-3 and TerraSAR-X with `timer unavailable` despite valid
current look angles. Using the locally cached
`jason3_cpf_261113_31701.hts` (HTS), the Jason-3 error was reproduced at
11:11:56 UTC: `CPF has insufficient records around requested epoch`.
Its header ends at 2026-11-18 00:00:00 UTC, 24 minutes after its last position
record. The previous countdown scanned the entire remaining header interval.
It now returns the first crossing and stops at actual position coverage or
header intent. The same local check now returns an event. TerraSAR-X's specific
failure has not been independently diagnosed.

## Required follow-up

- Preserve exact CPF files/providers used by both programs at capture; the
  locally cached Jason-3 file is not proof of the file used in the photos.
- Preserve the station configuration, terrestrial frame, and coordinate epoch
  used by both programs. The photographs show a 10.5° Arctra threshold but do
  not establish the full station coordinates; private configuration stays local.
- Export NASA predictions at identical UTC epochs with defined angle/range
  conventions, including which corrections apply and the Range field's units.
- Compare azimuth, elevation, range, AOS/LOS, and culmination at those epochs;
  investigate the first ETALON-1 timing discrepancy and WESTPAC differences.

Independent operational accuracy validation remains pending.
