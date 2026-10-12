# Avoid redundant owning `Value` self-assignment refcounts

The refreshed five-tree native sample after sparse cache allocation moved the
instruction-cache constructor out of the leading hotspots. `Value` copy
assignment and `release()` remained among the top sampled sites. The raw
[five-tree profile](data/async-tree-sparse-cache-native-samples-repeat5-20261005.json)
can be symbolized with
[`symbolize_native_windows.py`](../benchmarks/diagnostics/symbolize_native_windows.py).
An owning
`Value` assigned another owning `Value` that already refers to the same object
previously retained the source and released the destination, even though the
slot already owned exactly the reference it needed.

`Value::operator=(const Value&)` now recognizes that same-object owning case,
copies the non-borrowed flags, and returns without atomic refcount operations.
Borrowed destinations still use the existing retain-before-release path: they
must acquire their own reference. This is a generic runtime ownership fast
path; it does not replace any Python standard-library implementation.

## Evidence

- `xlang3_interpreter_tests` passes, including checks that same-object owning
  assignment does not change the refcount, borrowed destinations become
  owners, and copying a borrowed alias into an owning slot keeps ownership.
- The full Python fixture suite passes.
- The complete fixed Release regression gate passes against
  `build-repro/perf-control`. It uses 21 order-balanced pairs and five warmups;
  the largest candidate/control ratio is `function_calls` at `1.035x`, below
  the fixed `1.10x` limit. Raw results:
  [gate JSON](data/value-copy-self-assignment-fixed-release-gate-20261005.json).
- Official pyperformance 1.14 `async_tree_none`, through the Windows
  compatibility shim and Python 3.14.7 dependency site, measured `4.41 ±
  0.02 s` in fast mode. The preceding sparse-cache all-suite run measured
  `4.48 s`; pyperf reports the new run as `1.02x` faster. Treat this small
  fast-mode change as directional, not a statistically rigorous win. The raw
  result is [here](data/pyperformance-xlang3-value-copy-async-tree-none-20261005.json).
- The same-version CPython result in the complete comparison is `227.4 ms` for
  `async_tree_none`, so XLang3 remains about `19.4x` slower. This change is a
  small ownership-cost reduction, not a resolution of the async-tree gap.
- The 97-definition comparison and failure status remain in the
  [full pyperformance report](pyperformance-xlang3-sparse-instr-cache-vs-cpython314-fast-20261005.md).

Candidate executable SHA-256:
`12CB6F3C7C8EE1AEA620DD0E301A533F0653123B8BBECA162E23C29F10FC223E`.
Candidate runtime DLL SHA-256:
`708B203C9D641BAF0AAB2116922C7D69D933DB18452DAF072A4E9E784776D6CB`.

The overall speed goal remains open. The next profile investigation should
target call/frame execution and dictionary-backed runtime lookups; neither the
small async-tree movement nor the passing fixed gate closes the roughly
nineteen-fold CPython gap.
