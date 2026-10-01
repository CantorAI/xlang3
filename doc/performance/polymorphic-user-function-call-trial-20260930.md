# Polymorphic Python function call trial (2026-09-30)

## Hypothesis

CPython 3.14 specializes exact-argument Python calls around the currently
loaded function, while XLang3's `CallSiteKind::UserFunction` cache requires the
same `FunctionObject` at the call site. This can miss repeatedly at
polymorphic sites such as the pure-Python unpickler's `dispatch[key[0]](self)`.

The candidate let the normal call analyzer inspect a changed Python-function
target once. If no narrower inline specialization applied, it promoted the site
to a type-based `PolymorphicUserFunction` state and called the current function
through XLang3's ordinary frame-entry path. Function binding, defaults,
closures, tracing, monitoring, and fallback behavior still went through
`call_user_function` and `push_frame`; it did not change the Python library.

## Results

The first source-matched fast `unpickle_pure_python` pair suggested 3% faster,
but both samples warned about instability. Rigorous measurements reduced that
to a small, order-sensitive result: one order reported 1.01x faster
(`t=2.42`), while the reverse order was not significant. The candidate also
failed to show a repeatable DeltaBlue gain. Its 9% fast-screen result vanished
in both rigorous orders.

| Benchmark | Order | Control | Candidate | `pyperf compare_to` |
|---|---|---:|---:|---|
| `unpickle_pure_python` | Fast, control then candidate | 3.45 ± 0.08 ms | 3.34 ± 0.05 ms | 1.03x faster; unstable screen |
| `unpickle_pure_python` | Rigorous, candidate then control | 3.41 ± 0.06 ms | 3.37 ± 0.16 ms | 1.01x faster, `t=2.42` |
| `unpickle_pure_python` | Rigorous, control then candidate | 3.38 ± 0.08 ms | 3.37 ± 0.17 ms | Not significant |
| `deltablue` | Fast, control then candidate | 43.1 ± 3.3 ms | 39.7 ± 1.3 ms | 1.09x faster; unstable screen |
| `deltablue` | Rigorous, candidate then control | 39.2 ± 1.5 ms | 39.0 ± 1.2 ms | Not significant |
| `deltablue` | Rigorous, control then candidate | 40.2 ± 0.8 ms | 40.4 ± 0.7 ms | Not significant; 1.00x slower |

Because the apparent gains did not repeat in opposite-order rigorous runs, the
candidate was rejected. No fixed Release regression gate was run for it.

## Build identities and raw data

Both builds use the same executable SHA-256,
`8B514A12CB91479CD67591F9D2BA159DF33FED36E913EDF3E67585CC33DAB1DA`. The
source-matched control runtime SHA-256 is
`B80E9EE7CDFE171CC89689050BC75C9A32D2DC606FF1D5005A05552FB6374624`; the
candidate runtime SHA-256 is
`73E0F90E17A6D9572E6B9E807EBEFF58B793D26799F2B355225419DBF68729D5`.
The control was rebuilt from the committed source after detecting that an
older saved control DLL predated the `_functools` compatibility fix; only the
source-matched results above are used here.

Raw pyperf JSON files:

- `unpickle_pure_python`: [fast control](data/pyperf-polymorphic-user-function-control-unpickle-fast-20260930.json), [fast candidate](data/pyperf-polymorphic-user-function-candidate-unpickle-fast-20260930.json), [rigorous control, first order](data/pyperf-polymorphic-user-function-control-unpickle-rigorous-20260930.json), [rigorous candidate, first order](data/pyperf-polymorphic-user-function-candidate-unpickle-rigorous-20260930.json), [rigorous control, reverse order](data/pyperf-polymorphic-user-function-control-unpickle-repeat-rigorous-20260930.json), and [rigorous candidate, reverse order](data/pyperf-polymorphic-user-function-candidate-unpickle-repeat-rigorous-20260930.json).
- `deltablue`: [fast control](data/pyperf-polymorphic-user-function-control-deltablue-fast-20260930.json), [fast candidate](data/pyperf-polymorphic-user-function-candidate-deltablue-fast-20260930.json), [rigorous control, first order](data/pyperf-polymorphic-user-function-control-deltablue-rigorous-20260930.json), [rigorous candidate, first order](data/pyperf-polymorphic-user-function-candidate-deltablue-rigorous-20260930.json), [rigorous control, reverse order](data/pyperf-polymorphic-user-function-control-deltablue-repeat-rigorous-20260930.json), and [rigorous candidate, reverse order](data/pyperf-polymorphic-user-function-candidate-deltablue-repeat-rigorous-20260930.json).

These measurements rule out this cache transition as a material answer to the
CPython gap. The earlier exact-identity polymorphic cache trial was also
neutral; future call work should target the measured frame-entry and generic
dispatch costs rather than add another per-call-site identity cache.
