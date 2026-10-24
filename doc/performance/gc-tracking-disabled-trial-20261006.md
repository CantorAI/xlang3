# GC object-tracking disable trial (2026-10-06)

## What the code does

XLang3 does not run an automatic generational collection when objects are
allocated. It does, however, maintain an object registry unconditionally:
eligible objects are added to a process-wide vector under a mutex and removed
on final release. The `gc` module exposes that registry through
`gc.get_objects()` and uses it during explicit `gc.collect()` to find a
limited set of weak-reference-related cycles. The full CPython cyclic-GC API
is not implemented (`gc.get_count()` reports zero and the pyperformance
`gc_collect` benchmark fails its general cycle-collection expectation).

## Performance probe

I built a diagnostic Release candidate with both registry operations disabled
for the process. This preserves ordinary reference counting but also disables
`gc.get_objects()` enumeration and explicit cycle collection, so it was not
eligible to retain without an intentional change to those semantics.

| Official pyperformance 1.14.0 workload | Current-main control | Tracking-disabled candidate | Result |
| --- | ---: | ---: | --- |
| `async_tree_none` | 4.50 s ± 0.05 s | 4.46 s ± 0.04 s | 1.01× faster; single fast run |
| `pickle` | saved current-main result | candidate 17.3 µs ± 1.1 µs | `pyperf compare_to` not significant |

The async-tree change is only about one percent and has not been repeated to
establish a stable gain. The pickle comparison is hidden by pyperf as not
significant. Disabling the registry therefore does not currently support the
semantic tradeoff or explain the large interpreter slowdowns. No change was
retained; the Release executable and runtime DLL were restored to their
current-main hashes.

Both candidate runs used CPython **3.14.7** at `C:\Python\Python314`,
pyperformance **1.14.0**, and the same compatibility shim and dependency site.

- [Async-tree candidate JSON](data/pyperformance-gc-tracking-disabled-async-tree-candidate-fast-20261006.json)
- [Async-tree comparison](data/pyperformance-gc-tracking-disabled-async-tree-compare-20261006.txt)
- [Pickle candidate JSON](data/pyperformance-gc-tracking-disabled-pickle-candidate-fast-20261006.json)
- [Pickle comparison against the saved current-main run](data/pyperformance-gc-tracking-disabled-pickle-compare-20261006.txt)

The registry mutex is a real shared runtime cost, but this probe says it is a
minor contributor on these workloads. Further work should focus on VM
execution and coroutine/task resume paths.
