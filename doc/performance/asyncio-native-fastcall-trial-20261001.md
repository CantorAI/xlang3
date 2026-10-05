# Asyncio native-method fast-call trial (2026-10-01)

**Status: semantic tests and fixed-baseline gate pass; the targeted improvement
is small and not separated from pyperf noise.** Keep the guarded fast callbacks
as a low-risk optimization, but do not count them as closing the asyncio gap.

## Why this path was changed

The official full run measured XLang3's `async_tree_eager` at **1.901 s** versus
CPython 3.14.7 at **88.48 ms**, a **21.5×** slowdown. The pyperformance workload
builds a six-level tree with six branches per level, scheduling roughly 56,000
child Tasks. CPython's native `_asyncio` calls direct C methods with borrowed
arguments and stores Future/Task fields inline in its object structs.

The XLang3 diagnostic probe for a smaller tree counted hundreds of calls to
`Future.done`, `cancelled`, `exception`, `result`, callback registration, and
the awaited-by helpers on native paths that had no fast callback. These methods
now use the existing VM register-backed native-method adapter. `cancel` and
`add_done_callback` retain their keyword callback for keyword calls; their
ordinary positional form can use the fast adapter. The implementation still
runs the same native callback after the normal type/method dispatch, so
subclass overrides and descriptors remain observable.

The changes are in [`asyncio_future.cpp`](../../src/runtime/modules/system/asyncio_future.cpp),
[`asyncio_module.cpp`](../../src/runtime/modules/system/asyncio_module.cpp),
and [`contextvars_module.cpp`](../../src/runtime/modules/system/contextvars_module.cpp).
The comments explain the hot call counts and the expected VM argument path.

## Results

All samples use pyperformance 1.14.0, the same CPython 3.14 dependency site,
and the command-safe Windows shim. `debug` gives one score per invocation;
`fast` is pyperf's short distribution and still warns about sample stability.

| Variant | `async_tree_eager` samples | Median / mean |
|---|---:|---:|
| Before native fast callbacks, debug | 1.79, 1.84, 1.83 s | median 1.83 s |
| Fast callbacks, debug | 1.89, 1.79, 1.81 s | median 1.81 s |
| Before native fast callbacks, `fast` | mean 1.83 s ± 0.03 s | 1.83 s |
| Fast callbacks, `fast` | mean 1.80 s ± 0.03 s | 1.80 s |
| CPython 3.14.7, full-run reference | mean 88.48 ms | 88.48 ms |

The short-run mean suggests about a **1.6%** reduction, but both pyperf
distributions have 30 ms standard deviation and the candidate is still about
**20.3× slower** than CPython. Treat the improvement as unconfirmed until a
longer rigorous paired distribution reproduces it.

An instrumented smaller-tree probe also moved frequent method invocations off
the generic native-call route: the original probe showed `Future.done` 686,
`cancelled` 684, `exception` 683, `result` 345, `add_done_callback` 346, and
each awaited-by helper 340 times as slow calls. With the adapters those names
left the slow-call list and total fast/cached-fast calls increased. The probe
measures call-path selection, not elapsed-time performance.

The follow-up exact-type caching idea was rejected: using `_asyncio`'s cached
Future/Task symbols in `exact_future()` measured 1.82, 1.80, and 1.82 s in
separate debug samples against the overlapping 1.79–1.84 s control range.
The follow-up native-symbol `string_view` index was also rejected: on top of
the fast-call candidate, pyperformance measured 1.79 ± 0.03 s for control and
1.82 ± 0.03 s for the candidate. Its extra non-owning key lifetime complexity
did not produce a gain, so the source and Release output were restored to the
fast-call control.

The follow-up reusable keyword-vector scratch for `call_soon(context=...)` was
also rejected. Three `debug` samples had the same **1.81 s median** on both
sides (control 1.79, 2.05, 1.81 s; candidate 1.80, 1.81, 1.85 s). The pyperf
`fast` result was 1.79 ± 0.03 s for control and 1.82 ± 0.02 s for the
candidate. This removed keyword-vector allocation was not the dominant cost;
the source and Release output were restored to the fast-call control.

Raw benchmark evidence:

- [Fast-call control debug 1](data/async-tree-exact-future-cache-control-r1-20261001.json)
- [Fast-call control debug 2](data/async-tree-exact-future-cache-control-r2-20261001.json)
- [Fast-call control debug 3](data/async-tree-exact-future-cache-control-r3-20261001.json)
- [Fast-call candidate debug 1](data/async-tree-native-fastcall-candidate-r1-20261001.json)
- [Fast-call candidate debug 2](data/async-tree-native-fastcall-candidate-r2-20261001.json)
- [Fast-call candidate debug 3](data/async-tree-native-fastcall-candidate-r3-20261001.json)
- [Fast-call control `fast` run](data/async-tree-native-fastcall-control-fast-20261001.json)
- [Fast-call candidate `fast` run](data/async-tree-native-fastcall-candidate-fast-20261001.json)
- [Exact-type cache control and candidate logs](data/async-tree-exact-future-cache-control-r1-20261001.log), [candidate repeats](data/async-tree-exact-future-cache-candidate-r1-20261001.log)
- [Symbol-view control](data/async-tree-native-fastcall-symbol-view-control-fast-20261001.json)
- [Symbol-view candidate](data/async-tree-native-fastcall-symbol-view-candidate-fast-20261001.json)
- [Keyword-scratch control samples](data/async-tree-call-soon-keyword-scratch-control-r1-20261001.json), [candidate samples](data/async-tree-call-soon-keyword-scratch-candidate-r1-20261001.json), [candidate fast distribution](data/async-tree-call-soon-keyword-scratch-candidate-fast-20261001.json)
- [Native-call counters before](data/async-tree-native-fastcall-control-counters-20261001.txt)
- [Native-call counters after](data/async-tree-native-fastcall-candidate-counters-20261001.txt)

## Validation

- `tests/native/asyncio_accelerator.py`: passed under XLang3 and CPython 3.14.
- `tests/fixtures/core/context_run_keywords.py`: matching output under the
  preserved control, candidate, and CPython 3.14.
- Release CTest: **55/55 passed**.
- Fixed Release regression gate: **pass**, all 11 cases, 21 paired samples,
  default 10% threshold. The report is
  [`asyncio-native-fastcall-fixed-baseline-20261001.json`](data/asyncio-native-fastcall-fixed-baseline-20261001.json).
- The gate candidate/control runtime DLL SHA-256 is
  `EBC04BEF7E...`; the gate executable SHA-256 is
  `3AFB3E841A...`. Full hashes are in the gate JSON and preserved control.

The next large target is still the asyncio Task/Handle call path. CPython's
native Task step uses a specialized `TaskStepMethWrapper`; XLang3 currently
creates a bound native method for the Task callback. The `call_soon` keyword
vector itself was tested and was not the dominant cost. Task Handle callbacks
also reach `Context.run` through star-expanded `CallMethodEx`, which currently
materializes positional arguments. Profile those transitions and optimize the
shared VM call machinery without changing Python `asyncio` code.
