# Avoid duplicate active-exception restoration on native-to-Python calls (2026-10-06)

## Change and result

Keep the `Interpreter::run_function()` active-exception guard as the owner of
the handled-exception boundary. `runtime_call_callable()` previously saved the
same state before entering a Python function and restored it again after
`run_function_value()` returned. Removing that duplicate save/restore gives
native-to-Python callbacks one fewer exception-state copy and restore. The
comment beside the call path records why this boundary is performance
sensitive and where exception restoration belongs.

The official fast-mode comparisons show a repeatable **1–2% improvement** on
both asyncio tree variants. The change is retained as a small shared-runtime
gain; XLang3 remains far slower than CPython on these workloads, so this is not
a resolution of the performance goal.

## Python 3.14.7 comparison

Both runtimes used pyperformance 1.14.0 and the same Python 3.14.7 dependency
site. XLang3 control and candidate ran on the same host. The main screen was
run in both orders; candidate-first and control-first both favored the
candidate on `async_tree_none`.

| Benchmark | Fixed Release control | Candidate screen 1 | Candidate screen 2 | Assessment |
| --- | ---: | ---: | ---: | --- |
| `async_tree_none` | 3.62 s ± 0.02 s | 3.58 s ± 0.03 s | 3.56 s ± 0.02 s | `pyperf compare_to`: 1.01–1.02× faster in both comparisons |
| `async_tree_eager` | 1.29 s ± 0.01 s | 1.27 s ± 0.01 s | — | 1.02× faster in one fast-mode pair |
| `pickle_pure_python` | 5.21 ms ± 0.06 ms | 5.17 ms ± 0.06 ms | 5.17 ms ± 0.09 ms | First pair 1.01× faster; second not significant |
| `subparsers` | 145 ms ± 2 ms | 148 ms ± 2 ms | 145 ms ± 2 ms | First pair slower; second at parity |
| `logging_format` | 90.5 µs ± 1.5 µs | 91.9 µs ± 1.4 µs | 90.0 µs ± 1.5 µs | Direction changed with order; no retained regression claim |
| `logging_silent` | 1.06 µs ± 0.01 µs | 1.07 µs ± 0.01 µs | 1.07 µs ± 0.03 µs | No significant change |
| `logging_simple` | 84.0 µs ± 1.2 µs | 84.6 µs ± 0.9 µs | 84.0 µs ± 0.8 µs | No significant change |

The geometric mean for the first five-case comparison was 1.00× slower; the
second comparison found only `async_tree_none` significant, also at an overall
1.00× ratio. This change therefore contributes a targeted async improvement,
not a suite-wide gain. The existing saved CPython 3.14.7 reference is 227.4 ms
for `async_tree_none` and 86.62 ms for `async_tree_eager`; even the candidate
remains about **15.7×** and **14.7× slower**, respectively.

## Validation and artifacts

- The full `tests/run_fixtures.py` runner passed against the candidate.
- `xlang3_interpreter_tests.exe` passed.
- Candidate `xlang3.exe` SHA-256:
  `895962ED64351EE35D295F2C55593BCCD6A02E571E6EF5828FBF9E2E2A8438A4`.
- Candidate `xlang3_runtime.dll` SHA-256:
  `C85B51A0DEF9C463E4B29A89097CFD3A77DC35B514CF571F5D815483003E7FA4`.
- Fixed Release `xlang3.exe` and runtime hashes remained
  `A5F5028C15E145EDCE645A5AFC25C11FBCE77F51E882312B1FBE06E63C72A4AF` and
  `BC1B9C0A8086F7E6FB0C037516DC9C1EEA20427FA887E3AA623714BC5EF5DA8D`.

Raw results:

- [Control screen](data/nested-active-exception-control-screen-20261006.json)
- [Candidate screen 1](data/nested-active-exception-candidate-screen-20261006.json)
- [Candidate screen 2](data/nested-active-exception-candidate-r2-screen-20261006.json)
- [Eager control](data/nested-active-exception-control-eager-fast-20261006.json)
- [Eager candidate](data/nested-active-exception-candidate-eager-fast-20261006.json)

All results are fast-mode screens. The full 97-case pyperformance suite has
not been rerun on this candidate. Continue profiling shared interpreter costs
and validate each retained change against CPython 3.14.7.
