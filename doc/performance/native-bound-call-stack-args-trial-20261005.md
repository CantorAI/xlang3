# Native bound-call stack-arguments trial (2026-10-05)

## Result

Rejected. Replacing the temporary vector used to prepend `self` for bound
native calls with an eight-slot stack buffer did not produce a significant
change in the official `async_tree_none` benchmark. The fixed parent measured
**5.09 s ± 0.06 s** and the candidate measured **5.08 s ± 0.05 s** in
pyperformance 1.14.0 fast mode. `pyperf compare_to` hid the result as not
significant. The candidate source change was removed; the parent implementation
remains in the runtime.

This rules out the argument vector as a meaningful cause of the large async
gap. The counter profile records many calls to `_contextvars.Context.run`, but
allocation avoidance at this shared bound-native boundary did not move the
official result.

## Candidate and comparison

The candidate changed only `runtime_call_callable` in
`src/runtime/functional_iterators.cpp`. For a bound native function with at
most seven explicit arguments, it copied `self` and arguments into a fixed
`std::array<Value, 8>` before entering the existing native callback path. The
standard heap-backed vector remained for Python functions and larger argument
lists. The intent was to avoid one small heap allocation per native callback
while preserving the existing callback and monitoring dispatch.

CPython 3.14.7's native task code calls `PyIter_Send` for native coroutine
steps and its `TaskStepMethWrapper` calls `task_step` directly. This trial
tested a more general argument-prepending optimization in XLang's runtime call
boundary; the official benchmark did not support keeping that change. See
[CPython 3.14.7 `_asynciomodule.c`](https://github.com/python/cpython/blob/v3.14.7/Modules/_asynciomodule.c#L1968-L1985).

Both builds used the official pyperformance 1.14.0 `async_tree` benchmark
(`async_tree_none`), CPython 3.14.7 as manager, the shared Python 3.14
dependency site, and fast mode:

| Runtime | Mean ± standard deviation |
|---|---:|
| XLang3 parent, active-context-pointer build | 5.09 s ± 0.06 s |
| XLang3 stack-arguments candidate | 5.08 s ± 0.05 s |

`pyperf compare_to --table --verbose` reported:

```text
Benchmark hidden because not significant (1): async_tree_none
```

The control and candidate used the same `xlang3.exe` SHA-256
`091105B9328CC1D1531E2E70FB86B9FDC23A8608E3BE8DDCBAD90BDB532A8B3F`.
Their runtime DLL hashes were respectively
`F03673BFD9CE90D77631B3C8EDB267802FD4D0BB0489498D80E9791BB9B3E038` and
`9A0F551DC257953E63AA195DE8540B30B0C6CA86EEFF8E3F7EDE034954295F8B`.

Raw data and logs:

- [Control JSON](data/async-tree-none-control-native-stackargs-fast-20261005.json) and [log](data/async-tree-none-control-native-stackargs-fast-20261005.log)
- [Candidate JSON](data/async-tree-none-candidate-native-stackargs-fast-20261005.json) and [log](data/async-tree-none-candidate-native-stackargs-fast-20261005.log)

The last full local Release gate during the trial passed all 11 cases against
the fixed main baseline; see the [gate JSON](data/native-bound-call-stackargs-fixed-release-gate-20261005.json).
Full CTest reported 54/55 passing; the only failure
was the existing Visual Studio debugpy profile assertion documented in the
[active-context-pointer trial](context-run-active-map-pointer-trial-20261005.md).
