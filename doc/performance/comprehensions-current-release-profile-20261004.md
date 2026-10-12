# Comprehension workload profile against CPython 3.14.7 (2026-10-04)

The rebuilt `build-repro/Release/xlang3.exe` ran the unchanged official
pyperformance 1.14.0 `comprehensions` benchmark in fast mode. The reference
was `C:\Python\Python314\python.exe`, CPython 3.14.7, using the same
pyperformance dependencies on the same Windows host.

| Runtime | Mean per benchmark loop | CPython / XLang3 |
|---|---:|---:|
| CPython 3.14.7 | 14.0 µs ± 0.2 µs | — |
| XLang3 Release | 173 µs ± 8 µs | XLang3 12.40× slower |

Both runs warned that fast mode did not establish stability at pyperf's 1%
target. The ratio is a fresh directional result for the current rebuilt
executable; do not treat it as a precise estimate.

## What the XLang3 profile showed

The benchmark itself creates `Widget` values, filters them through
`WidgetTray._is_big_spinny`, and sorts them using `WidgetTray._any_knobby`.
These are ordinary Python classes and methods from the pyperformance source;
the profile found no native-module fallback in this path. A 100-loop
`sys.setprofile` diagnostic counted 2,400 `_is_big_spinny` calls, 1,800
`_any_knobby` calls, and 3,000 generator-expression resumes. Its instrumented
function times attributed most of the measured workload to `_any_knobby` and
its generator expression. The profiler distorts timings substantially, so
these function times are for hotspot location only.

A separate 1,000-loop XLang3 VM counter run recorded about 1,990 dispatched
IR operations per benchmark loop. The most frequent were `Jump` (242),
`LoadLocalAttr` (175), `StoreLocal` (174), `ListAppend` (162), and
`CallMethod` (152). This supports focusing next on interpreted helper calls
and generator resumes in the shared VM. It does not support replacing the
pure-Python benchmark/library code with native C++.

## Evidence and validation context

- [XLang3 pyperf JSON](data/comprehensions-current-release-vs-cpython3147-xlang3-fast-20261004.json)
- [CPython 3.14.7 pyperf JSON](data/comprehensions-current-release-vs-cpython3147-cpython-fast-20261004.json)
- [XLang3 per-op counters](data/comprehensions-current-release-perf-counters-20261004.txt)
- [XLang3 Python-call profile](data/comprehensions-current-release-call-profile-20261004.txt)
- Current Release executable SHA-256: `14111A96239B1CEB87D9370C5FA938B4187E713F32ADBA8C1C91C60EBA598359`.

The focused VM test groups passed; 344 core and compatibility-section
fixtures passed. Four ctypes-dependent fixtures were skipped because the
installed Windows Python reports that libffi is unavailable. The overall
goal of beating CPython remains open. The next candidate must reduce a
measured shared VM cost and pass the same CPython 3.14.7 benchmark.
