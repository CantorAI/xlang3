# Immortal-string `Value` flag trial (2026-10-06)

## Result

Rejected. Copying an immortal-string hint into `Value::flags` did not improve
the large asyncio case or Python `pickle`, and pyperf found the JSON movement
insignificant. It measured `async_tree_eager` **1.02× slower** and
`logging_format` **1.03× slower**. The source change was removed and the
fixed Release executable/runtime pair was restored.

The profile showed `release(Value)` among the sampled hot symbols. The
hypothesis was that immortal strings paid an object-kind check and an atomic
`StringObject::immortal` load on every retain/release. The candidate marked
immortal ASCII and interned strings in `Value::flags`, allowing those Values to
skip that object load. Values without the hint retained the original checks.

| Benchmark | Fixed control | Candidate | pyperf result |
|---|---:|---:|---|
| `async_tree_none` | 4.51 s ± 0.07 s | 4.53 s ± 0.07 s | Not significant |
| `async_tree_eager` | 1.29 s ± 0.04 s | 1.32 s ± 0.04 s | 1.02× slower |
| `json_dumps` | 36.3 ms ± 8.0 ms | 34.6 ms ± 1.5 ms | Not significant |
| `logging_format` | 89.1 µs ± 1.4 µs | 92.1 µs ± 2.5 µs | 1.03× slower |
| `logging_silent` | 1.04 µs ± 0.02 µs | 1.05 µs ± 0.02 µs | Not significant |
| `logging_simple` | 84.5 µs ± 9.5 µs | 86.0 µs ± 1.8 µs | Not significant |
| `pickle_pure_python` | 5.19 ms ± 0.24 ms | 5.16 ms ± 0.12 ms | Not significant |

Each selected run used pyperformance 1.14.0 `--fast`. Several results warned
that their sample counts were unstable. The insignificant JSON movement is
not evidence of a gain; the two significant regressions rule out keeping this
per-Value branch change.

## Validation and build identities

The candidate compiled in Release and passed `xlang3_runtime_value_tests`,
`xlang3_interpreter_tests`, and the full fixture runner under CPython 3.14.7.
After removing the source change, those tests passed again. The fixed
`build-repro/Release` pair was restored byte-for-byte from the saved control.

| Build | `xlang3.exe` SHA-256 | `xlang3_runtime.dll` SHA-256 |
|---|---|---|
| Fixed control | `E8EFEE922E95093437E0FEF3F6754ED2730A3CEC2BBA9A4C067F2C660C7B202C` | `B29EE2944F3916F7D901EB2A178F58CD9EA1B36DC1D1DFB2033338012D61CC2D` |
| Candidate | `586D79F034BE321F0D5DA3CD445FA0EB846E6FEB5EC17BFF726A1B362E757A09` | `2E5BF6F7E33B342CB26A571F39D9C4F1688EB3F733DC1287DF310A64E49C1460` |

Both used CPython **3.14.7** as the pyperf manager, the same
`C:\Python\Python314\Lib\site-packages` dependency site, and the repository
compatibility shim. The experiment changed no Python standard-library code.

Raw pyperf files and comparison:

- [Control samples](data/immortal-string-flag-control-r1-20261006.json)
- [Candidate samples](data/immortal-string-flag-candidate-r1-20261006.json)
- [pyperf comparison](data/immortal-string-flag-compare-fast-20261006.txt)

This trial does not close the performance goal. The next investigation should
follow the sampled string-hash and attribute-lookup paths rather than adding
another retain/release guard.
