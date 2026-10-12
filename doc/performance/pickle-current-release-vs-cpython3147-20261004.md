# Current Release pure-Python pickle results vs CPython 3.14.7 (2026-10-04)

The rebuilt `build-repro/Release/xlang3.exe` was measured with official
pyperformance 1.14.0 `--rigorous` runs for `pickle_pure_python` and
`unpickle_pure_python`. The reference executable was exactly
`C:\Python\Python314\python.exe` (CPython 3.14.7). Both runs used the same
Windows host and pyperformance benchmark definitions. XLang3 used the shared
CPython 3.14.7 dependency site through the repository's compatibility shim.

| Benchmark | XLang3 Release | CPython 3.14.7 | CPython / XLang3 |
|---|---:|---:|---:|
| `pickle_pure_python` | 5.52 ms ± 0.55 ms | 259 µs ± 6 µs | XLang3 21.36× slower |
| `unpickle_pure_python` | 2.41 ms ± 0.17 ms | 170 µs ± 4 µs | XLang3 14.15× slower |

`pyperf compare_to` gives a 17.38× geometric mean slowdown for this two-case
pair. Both sides emitted sample-stability warnings; treat the numbers as a
fresh directional measurement, not as a precision claim. The previous
full-suite comparison used the same Python 3.14.7 standard library and remains
the broad coverage result. This focused rerun confirms that the current
rebuilt runtime has not closed the pure-Python serialization gap. The library
implementation stays Python; work should focus on XLang3's shared VM and
runtime costs, with each candidate judged by matched pyperf runs.

Validation on the rebuilt executable: `xlang3_runtime_value_tests` and
`xlang3_interpreter_tests` passed. The fixture runner passed 344 core and
compatibility-section cases; four ctypes-related fixtures were skipped because
the installed Windows Python reports `libffi is unavailable for ctypes`. No
other fixture failures occurred.

| Artifact | SHA-256 |
|---|---|
| XLang3 Release executable | `14111A96239B1CEB87D9370C5FA938B4187E713F32ADBA8C1C91C60EBA598359` |
| XLang3 runtime DLL | `005AEE1F60D016178EE29F219E3FF4B503BE65CD52F4C3DFAE5D69BA04886CEF` |

Raw measurements: [XLang3 JSON](data/pickle-current-release-vs-cpython3147-xlang3-rigorous-20261004.json), [CPython 3.14.7 JSON](data/pickle-current-release-vs-cpython3147-cpython-rigorous-20261004.json).
