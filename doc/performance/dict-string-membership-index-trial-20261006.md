# Exact-string dict membership index trial (2026-10-06)

## Result

Rejected as a performance change. The candidate used the existing exact-string
dict index for plain-dict membership when a miss was definitive, avoiding a
lookup result `Value` that `in dict` does not need. Misses in mixed-key dicts,
dict subclasses, module namespaces, and unsupported key shapes retained the
ordinary runtime path so custom equality and mutation semantics stayed intact.

The official pyperformance `argparse_subparsers` benchmark showed a small fast-
mode signal, but two order-balanced rigorous comparisons were statistically
insignificant. The code and added fixture edits were removed; the candidate is
not counted as a speedup. The exact control executable was restored at the
existing `build-repro/main-verify-20261006/Release` run path. No CMake configure
was run; the existing Visual Studio/Ninja Release tree was used.

## Measurements

All runs used pyperformance 1.14.0, CPython 3.14.7 at
`C:\Python\Python314`, the same installed dependency site, and the unchanged
official `bm_argparse/run_benchmark.py` body through the repository's
`run_pyperformance_xlang3_shimmed.py` harness.

| Mode and order | Control | Candidate | Interpretation |
|---|---:|---:|---|
| Fast pair 1, control then candidate | 153 ± 25 ms | 145 ± 3 ms | Directional only; control was noisy |
| Fast pair 2, candidate then control | 149 ± 15 ms | 144 ± 4 ms | Directional only; control was noisy |
| Rigorous pair 1, control then candidate | 148 ± 13 ms | 147 ± 11 ms | Not significant |
| Rigorous pair 2, candidate then control | 148 ± 14 ms | 147 ± 11 ms | Not significant |

`pyperf compare_to` hid both rigorous pairs as statistically insignificant.
The small apparent fast-mode reduction is not supported by the rigorous runs.
Since the measured effect is below the benchmark's noise, no full Release gate
or full 97-case suite was run for this candidate.

## Correctness and build identity

The candidate passed the complete `tests/run_fixtures.py` suite,
`xlang3_interpreter_tests.exe`, and `xlang3_runtime_value_tests.exe`. The
temporary `dict_views` fixture additions covered membership hits and misses,
index invalidation after mutation, custom key equality in mixed dicts, and
unhashable lookup errors; they were removed with the rejected optimization.

| Build | Executable SHA-256 | Runtime DLL SHA-256 |
|---|---|---|
| Restored control | `FF66E309BED7F3226F52F59E06842F448992F31805D54685EA755365CCD2D389` | `395BF96C94508A8E9E326D2C79C427489B729AC84CD244BACD4EDC037F331B1E` |
| Rejected candidate | `04C75EA0A5D70641B813E77053D77E7D98743B81D1D1B5380B87F76A671C605F` | `468DED0A20D3E438B779E4FEB353BF26C08C23DDF24BC882938A0926D748A521` |

The fixed `build-repro/Release` baseline was not modified. This trial does not
change the full-suite comparison or the overall CPython performance result.

## Raw pyperf data

- Fast control: [pair 1](data/pyperformance-subparsers-dictstr-control-r1-fast-20261006.json), [pair 2](data/pyperformance-subparsers-dictstr-control-r2-fast-20261006.json)
- Fast candidate: [pair 1](data/pyperformance-subparsers-dictstr-candidate-r1-fast-20261006.json), [pair 2](data/pyperformance-subparsers-dictstr-candidate-r2-fast-20261006.json)
- Rigorous control: [pair 1](data/pyperformance-subparsers-dictstr-control-r1-rigorous-20261006.json), [pair 2](data/pyperformance-subparsers-dictstr-control-r2-rigorous-20261006.json)
- Rigorous candidate: [pair 1](data/pyperformance-subparsers-dictstr-candidate-r1-rigorous-20261006.json), [pair 2](data/pyperformance-subparsers-dictstr-candidate-r2-rigorous-20261006.json)

The call-dispatch and frame costs remain the larger measured part of this
workload. The next `argparse_subparsers` attempt should target those costs with
a new mechanism; this string-membership shortcut is not supported by the
official benchmark result.
