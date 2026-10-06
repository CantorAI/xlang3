# Same-module globals guard trial (2026-10-06)

## Result

Rejected. Skipping the `CurrentGlobalsGuard` save/restore when the caller and
callee use the same module object preserved the full fixture and C++
interpreter-test results, but the official fast-mode screen found no
significant gain on `async_tree_none` or pure-Python pickle. `subparsers` was
2% slower, leaving the three-case geometric mean 1% slower. The source change
was removed.

Both builds include the previously retained active-exception guard change;
that code was identical between these two builds. This isolates the
same-module globals optimization.

## Matched results

| Benchmark | Control | Candidate | `pyperf compare_to` |
| --- | ---: | ---: | --- |
| `async_tree_none` | 3.57 s ± 0.02 s | 3.57 s ± 0.02 s | Not significant |
| `pickle_pure_python` | 5.14 ms ± 0.06 ms | 5.15 ms ± 0.05 ms | Not significant |
| `subparsers` | 146 ms ± 1 ms | 148 ms ± 2 ms | 1.02× slower |
| Geometric mean | — | — | 1.01× slower |

The candidate avoided retaining and releasing a duplicate reference only when
the current and incoming module globals were the same object. Other calls
kept the original save/restore path. This result shows that the extra Value
ownership at this guard is not a measurable async or pickle bottleneck on the
current Release build.

## Validation and artifacts

The full fixture runner and `xlang3_interpreter_tests.exe` passed against the
candidate. The candidate and control executables had the same SHA-256:
`895962ED64351EE35D295F2C55593BCCD6A02E571E6EF5828FBF9E2E2A8438A4`.
The control runtime DLL was
`AE327A39C506D01A02371C1C618CA6BA02BE2A18A81D49A73A9B0D7EF469C7BC` and the
candidate DLL was
`90598BADAD85DB7265B1A389BD4AC6A493CE091D5B7BCEC3EEDD939261ABA6F4`.
The fixed Release executable and DLL were untouched.

Raw pyperf results:

- [Control](data/same-globals-guard-control-screen-20261006.json)
- [Candidate](data/same-globals-guard-candidate-screen-20261006.json)

Continue with a different measured interpreter cost rather than adding a
module-specific guard to the call path.
