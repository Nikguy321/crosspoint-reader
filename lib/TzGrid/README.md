# TzGrid: the time zone map

Which time zone a measured position is in, for **Time Zone From Location** (Settings > Clock, X4
Pro): when Locate Me or Update Location When Syncing saves a location, `src/util/ClockZone` looks
it up here and may set the clock's zone. North America (with Alaska and the Aleutians, Hawaii,
Canada, Mexico, Central America and the Caribbean) at 0.05 degrees, run-length encoded: about
80 KB of flash, no RAM. A zone is answered only where neither the cell nor any of its eight
neighbours has a change of clock; `tz_grid.h` says the rest.

## Where it comes from

`tz_grid.h`, `tz_grid.cpp` and `tz_grid_data.h` are copied **unchanged** from the
[WiPhone firmware repository](https://github.com/Nikguy321/wiphone-meshtastic) (`WiPhone/`), where
`tools/gen_tz_grid.py` generates `tz_grid_data.h` and the test vectors from the public inputs below
and checks every covered zone's rule against zoneinfo for 2026-2035. COVEY carries the same map.
Keep the three files byte-identical to their source (they are left out of `bin/clang-format-fix`
for that reason): to update them, regenerate there and copy all three, with
`tests/vectors_tzgrid.json` into `test/tz_grid/`. `test/tz_grid` (TzGridTest) recomputes the
content hash from the compiled arrays and runs the WiPhone's own vectors, so a copy that answers
anything differently fails.

Data release: timezone-boundary-builder 2026d, tzdata 2026d, content hash (SHA-256)
`78d5937550f71e2c8ac8697276f028aa8a4ad8ebebcabcfd1b6ded83c9489a30`.

## Licence of the data (ODbL 1.0)

The arrays in `tz_grid_data.h`, and the test vectors in `test/tz_grid/vectors_tzgrid.json`, are a
Derivative Database of [timezone-boundary-builder](https://github.com/evansiroky/timezone-boundary-builder)
(release 2026d, `combined.json`), which is derived from **OpenStreetMap** data,
**© OpenStreetMap contributors**, and made available under the
[Open Database License (ODbL) 1.0](https://opendatacommons.org/licenses/odbl/1-0/). These files are
offered under the ODbL 1.0 as well; the means to remake them is the generator named above.

The same notice is compiled into the firmware (`tzGridInfo()->attribution`), and the Clock
settings screen credits "© OpenStreetMap contributors (ODbL)" under Time Zone From Location.
The ODbL covers the database contents only, not the code around them.

## Licence of the code

`tz_grid.h` and `tz_grid.cpp` were written for this fork's owner and are shared with his WiPhone
firmware, where the repository as a whole is GPL v3. The same author offers them here under this
repository's MIT licence. The data in `tz_grid_data.h` remains under the ODbL 1.0, as described above.

