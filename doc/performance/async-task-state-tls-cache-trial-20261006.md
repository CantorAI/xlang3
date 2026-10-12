# Async Task-state TLS cache trial (2026-10-06)

## Result

Rejected. Caching the current runtime's `ThreadTaskState*` in thread-local
storage did not improve the official `async_tree_none` score. The source
change was removed, the targeted C++ tests and full fixture runner passed on
the restored source, and the fixed Release executable/runtime pair was
restored byte-for-byte.

| Build | `async_tree_none` |
| --- | ---: |
| Control | 4.49 s ± 0.05 s |
| TLS pointer-cache candidate | 4.52 s ± 0.05 s |

`pyperf compare_to` reported the candidate at **1.01× slower**. Both official
fast-mode runs warned that the sample counts were insufficient for a stable
estimate, so this small movement does not support keeping the extra pointer
cache and invalidation paths. A quick alternating direct-body check produced
medians of 4.851 s for control and 4.791 s for candidate; its direction
disagreed with the official samples and confirms that the apparent 1% shift
is within run variation.

## Profile and hypothesis

A temporary, environment-gated timer around the native Task transition
measured 65,320 steps in the exact `async_tree_none` body. Its per-stage totals
were:

| Stage | Total |
| --- | ---: |
| `enter_task` | 837.6 ms |
| Direct coroutine resume | 2,093.2 ms |
| Handling yielded values | 90.7 ms |
| `leave_task` | 7.9 ms |

These instrumented totals are diagnostic, not the benchmark score; other
event-loop and Python execution work accounts for the rest of the wall time.
The candidate cached a stable per-runtime state pointer to reduce repeated
`unordered_map<Runtime*, ThreadTaskState>` lookups during enter/current-task
checks. Its direct-body timing moved slightly in the favorable direction,
but official pyperf did not confirm a gain. No timer or pointer cache remains
in the source.

## Validation and artifacts

- `xlang3_runtime_value_tests` passed.
- `xlang3_interpreter_tests` passed.
- The full fixture runner passed under CPython **3.14.7** at
  `C:\Python\Python314`.
- Restored fixed Release executable SHA-256:
  `E8EFEE922E95093437E0FEF3F6754ED2730A3CEC2BBA9A4C067F2C660C7B202C`.
- Restored fixed Release runtime DLL SHA-256:
  `B29EE2944F3916F7D901EB2A178F58CD9EA1B36DC1D1DFB2033338012D61CC2D`.
- Candidate executable SHA-256:
  `516E3C57A4EEBF1433F38BDBA35E2F93DEA3C1B42754DD8104CD88C70469ABFE`.
- Candidate runtime DLL SHA-256:
  `617A589FF4EDB791C7029D086478814CA877AF348BCAEBF2A92DCAE21572B2CE`.

The official pyperformance 1.14.0 runs used CPython 3.14.7 as manager, the
shared Python 3.14 site-packages, and the repository Windows compatibility
shim. Raw samples and comparison are preserved here:

- [Control pyperf JSON](data/async-task-tls-cache-control-fast-20261006.json)
- [Candidate pyperf JSON](data/async-task-tls-cache-candidate-fast-20261006.json)
- [pyperf comparison](data/async-task-tls-cache-compare-fast-20261006.txt)

The measured Task body suggests coroutine resume is the largest of the
instrumented Task stages, while Task entry consumes about 0.84 s across the
case. The next optimization should target the VM resume or general Python
execution cost with a measured before/after benchmark, not another TLS lookup
cache.
