# `_contextvars.Context.run` exact-tuple expansion trial (2026-10-06)

This trial checked whether `Context.run(callback, *args)` could skip the
generic expansion vector for asyncio handles by copying an exact, small tuple
into a bounded stack argument array. It was restricted to the native
`_contextvars.Context.run` call shape; arbitrary iterables, keywords, and other
call forms kept the existing path. The candidate built, and
`context_run_empty_star.py` produced the same output with the candidate and the
fixed Release executable, including its nonempty tuple and iterable cases.

## Paired XLang3 result

Both binaries were run with pyperformance 1.14.0 in fast mode on Python 3.14.7
using the same host and the same two asyncio benchmarks. The candidate was
built in the existing scratch directory; the fixed Release executable was not
rebuilt or changed.

| Benchmark | Fixed Release control | Candidate | Candidate speed ratio |
|---|---:|---:|---:|
| `async_tree_none` | 3.67 s ± 0.04 s | 3.61 s ± 0.03 s | 1.02× faster |
| `async_tree_eager` | 1.30 s ± 0.03 s | 1.30 s ± 0.04 s | no significant change |

```text
async_tree_none   control ██████████████████████████████████████ 3.67 s
                  trial   █████████████████████████████████████  3.61 s  (1.02×)
async_tree_eager  control ██████████████                         1.30 s
                  trial   ██████████████                         1.30 s  (not significant)
```

`pyperf compare_to` hides `async_tree_eager` as statistically insignificant
and reports only a 1.01× geometric-mean improvement. This does not justify
keeping another specialized call route: warmed expansion storage already
reuses capacity, and the actual callback plus Context entry costs dominate the
small amount of tuple flattening removed here. The C++ fast-path experiment
was reverted. The persistent general rule is to keep exact-shape native-call
shortcuts only when a paired pyperformance result shows a repeatable gain;
do not retry this tuple-stack approach without new evidence that expansion
materialization has become a larger cost.

## Reproduction and data

The fixed Release executable SHA-256 was
`A5F5028C15E145EDCE645A5AFC25C11FBCE77F51E882312B1FBE06E63C72A4AF`; the
candidate executable SHA-256 was
`65978E5B4769B415F8DACC0DA30E84AB98B49CE4CC6A07CBC1C267E83CF386B3`.

Raw pyperf files: [control](data/pyperformance-xlang3-contextvars-star-control-async-fast-20261006.json),
[candidate](data/pyperformance-xlang3-contextvars-star-candidate-async-fast-20261006.json).

The run command used `C:\Python\Python314\python.exe`, the repository's
`run_pyperformance_xlang3_shimmed.py`, `--mode fast`, and
`--case-timeout-override async_tree=600`.
