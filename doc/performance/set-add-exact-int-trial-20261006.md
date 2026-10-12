# Incremental integer set-add index trial (2026-10-06)

I tested extending the existing set hash index as exact integers were inserted,
so repeated `SetAdd` operations would probe hash collisions rather than scan
every existing value. The candidate Release build passed interpreter tests and
the full fixture suite, including the set equality/hash compatibility fixtures.

The selected `pyperformance` fast sample was 170 ± 7 µs for control and 167 ±
2 µs for candidate; `pyperf compare_to` hid it as statistically insignificant.
This is not evidence of a performance gain, and the candidate is not retained.

After the run, I checked the official benchmark profile. Its hot path builds
`Widget` objects, calls `_is_big_spinny` and `_any_knobby`, and resumes generator
expressions; it is not the local integer set-comprehension probe. The measured
official case therefore does not validate this set-add optimization, so future
work on `comprehensions` should follow the profiled generator/call path rather
than assume the repository's similarly named probe is representative.

Raw data: [control](data/pyperformance-set-add-exact-int-comprehensions-control-fast-20261006.json),
[candidate](data/pyperformance-set-add-exact-int-comprehensions-candidate-fast-20261006.json).
See the [official workload profile](comprehensions-current-release-profile-20261004.md)
for the measured hotspot and operation counts.
