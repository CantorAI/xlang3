# Direct construction for the native asyncio Future type — 2026-10-04

## Change

XLang3's native `_asyncio.Future` now has an internal full-constructor callback
for the common zero-argument call to the exact, unmodified native class. It
creates the native instance and Future state, then runs the same
`initialize_future()` routine used by `Future.__init__`. This avoids generic
`__new__`/`__init__` lookup and argument adaptation at the class-call site.

The callback is version-guarded against class mutation. Subclasses, explicit
`loop=` calls, argument expansion, and classes with changed `__new__` or
`__init__` methods retain the generic class-construction path. This is an
XLang3 native implementation of the CPython-native `_asyncio.Future` type; it
does not replace pure-Python `asyncio` library code. The performance rationale
and invalidation rule are documented beside the class metadata and callback.

## Measurement

Two `pyperformance 1.14.0 --fast` runs per build were executed in
control/candidate/candidate/control order against Python 3.14.7:

| Build | Run 1 | Run 2 | Merged result |
|---|---:|---:|---:|
| Control | 2.89 s ± 0.03 s | 2.88 s ± 0.05 s | 2.88 s ± 0.04 s |
| Candidate | 2.82 s ± 0.04 s | 2.83 s ± 0.05 s | 2.83 s ± 0.04 s; 1.02× faster |

`pyperf compare_to -v` reports the difference as significant (`t=5.78`). The
samples remain fast-mode measurements, and the change reduces the remaining
`async_tree_eager` time only modestly: it is still about 32.7× CPython's
86.62 ms result.

| Build | Executable SHA-256 | Runtime DLL SHA-256 |
|---|---|---|
| Control | `2AC2B070D81DF0FE9DCC09F522CC9FD0BA96A085963D2240D405B3E4EA5D5B5E` | `0438CAC8D62F4729F891208251A9CB65CC036F8E2D22F245E39E7B412B098F6E` |
| Candidate used for A/B | `3B54EB775CB828B5C722A0ED67FAB005AAF8AF6682C49CAB063CE5467C170806` | `C86AF9EDB75FD116657FDD8383045FB02E82B3291921F9959E792AC3C36E7B55` |

## Full pyperformance rerun on the final candidate

The current Release binary was run against all 97 definitions with Python 3.14.7 and pyperformance 1.14.0. It produced 45 benchmark results and recorded 52 failures/timeouts. Among 49 matched subtests, 5 favored XLang3; the geometric mean CPython/XLang3 ratio was 0.16483x. This candidate therefore does not change the overall conclusion: XLang3 remains substantially slower on the comparable suite. `telco` (0.031x), `async_tree_eager` (0.031x), pure-Python pickle (0.053x), `subparsers` (0.056x), and `logging_silent` (0.064x) remain the largest measured gaps.

See the [full 3.14 comparison and rightward ratio chart](../pyperformance-xlang3-asyncio-future-direct-vs-cpython314-fast-20261004.md), [all-97 status CSV](pyperformance-xlang3-asyncio-future-direct-vs-cpython314-fast-20261004-all-97-status.csv), and [matched subtest CSV](pyperformance-xlang3-asyncio-future-direct-vs-cpython314-fast-20261004-subtests.csv). The complete XLang3 JSON and log are linked in that comparison.
## Validation

- `asyncio_native_future_constructor.py` matches CPython 3.14. It checks
  default construction, explicit `loop=` fallback, and a subclass-defined
  `__init__` that calls the native base initializer.
- `xlang3_interpreter_tests.exe` passed after rebuilding against the new class
  metadata.
- All 11 fixed Release regression cases passed against
  `baseline-0336992` using 7 order-balanced pairs and 2 warmups. The largest
  candidate/baseline ratio was `range_for` at 1.021×, well under the 1.10
  threshold. See the [gate data](data/asyncio-future-direct-constructor-fixed-baseline-gate-20261004.json).
- The current rebuilt candidate is `xlang3.exe` SHA-256
  `CC0EE6BFF8057EC8B87D5714B96DEDD8111FB58BA014005242ECB8DFD4BCEB66` with
  `xlang3_runtime.dll` SHA-256
  `491942367F67BF264B49C34301BD34A91E31DB64039F40FD6974145CB6C55A5D`.

## Raw results

- Control run 1: [`JSON`](data/async-tree-future-direct-control-fast-20261004.json), [`log`](data/async-tree-future-direct-control-fast-20261004.log)
- Candidate run 1: [`JSON`](data/async-tree-future-direct-candidate-fast-20261004.json), [`log`](data/async-tree-future-direct-candidate-fast-20261004.log)
- Candidate run 2: [`JSON`](data/async-tree-future-direct-candidate-r2-fast-20261004.json), [`log`](data/async-tree-future-direct-candidate-r2-fast-20261004.log)
- Control run 2: [`JSON`](data/async-tree-future-direct-control-r2-fast-20261004.json), [`log`](data/async-tree-future-direct-control-r2-fast-20261004.log)
- [Merged control](data/async-tree-future-direct-control-merged-fast-20261004.json) and [merged candidate](data/async-tree-future-direct-candidate-merged-fast-20261004.json)

The next required check is a fresh full 97-definition pyperformance run on the
current rebuilt candidate; the overall CPython speed objective remains open.
