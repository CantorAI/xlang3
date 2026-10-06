# Polymorphic `type(value)` result-cache trial (2026-10-06)

## Result

Rejected. A four-entry result cache for stable builtin `ValueTag` and object
kinds did not improve the official deepcopy or pure-Python pickle benchmarks.
`pyperf compare_to` found `deepcopy` and `deepcopy_memo` about **1.05× slower**;
it hid `deepcopy_reduce` and `pickle_pure_python` as insignificant. The
four-entry state and lookup were removed.

| Benchmark | Single-result cache control | Polymorphic candidate | Candidate / control |
|---|---:|---:|---:|
| `deepcopy` | 2.47 ms ± 0.10 ms | 2.60 ms ± 0.21 ms | 1.05× slower |
| `deepcopy_reduce` | 27.3 μs ± 1.6 μs | 29.0 μs ± 5.6 μs | Not significant |
| `deepcopy_memo` | 263 μs ± 12 μs | 275 μs ± 4 μs | 1.05× slower |
| `pickle_pure_python` | 5.22 ms ± 0.46 ms | 5.17 ms ± 0.12 ms | Not significant |

```text
Relative elapsed time; lower is faster (bars extend left to right)
deepcopy control       2.47 ms  |####################
deepcopy candidate     2.60 ms  |#####################
deepcopy_memo control   263 μs  |####################
deepcopy_memo candidate 275 μs  |#####################
```

All samples emitted pyperf fast-mode stability warnings. These results are a
screen, and show no benefit worth retaining. The previous single-result
`type(value)` call specialization remains unchanged.

## Hypothesis and decision

The `copy` and `pickle` Python implementations repeatedly call `type(value)`
from a small number of call sites as they visit heterogeneous object graphs.
The existing VM specialization cached the exact builtin `type` callee but
still resolved the result on every hit. I tried a four-entry per-site cache
for scalar tags and object kinds with stable builtin types, with ordinary
resolution for instances, files, modules, classes, and other dynamic kinds.
The cache retained the resolved type objects and guarded each result by the
argument tag/kind.

Avoiding type-table resolution did not lower end-to-end time. The extra cache
search and retained state outweighed any saved lookup on these workloads. No
pure-Python standard-library code was changed.

## Reproduction data

- [Single-result cache control pyperf JSON](data/pyperformance-copy-pickle-classkey-control-20261006.json)
- [Polymorphic candidate pyperf JSON](data/pyperformance-copy-pickle-typepic-candidate-20261006.json)
- [Verbose pyperf comparison](data/pyperformance-copy-pickle-typepic-compare-20261006.txt)
- Python harness: CPython 3.14.7; pyperformance 1.14.0; Windows x64; `--fast`.
- Candidate used the scratch Release build under `build-repro/tuple2-candidate`; the fixed `build-repro/Release` executable was untouched.
