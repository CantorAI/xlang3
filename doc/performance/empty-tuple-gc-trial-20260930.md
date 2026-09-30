# Empty-tuple GC registration trial (2026-09-30)

## Finding

Ordinary Python calls bind an empty `*args` tuple when the callee accepts variadic positional arguments and the caller passes none. `logging_silent` exercises this path repeatedly through `Logger.debug` and `Logger.isEnabledFor`. The XLang3 collector's `gc_value_is_tracked()` already reports an empty tuple as untracked because it cannot contain references, but tuple allocation still inserted it into the collector's shared tracked-object index. That insertion takes the global tracking mutex.

The runtime now skips `gc_track_object()` for zero-capacity tuples, both when allocating a fresh tuple and when reusing one from the per-thread tuple free list. Tuples with one or more slots keep the existing tracking path. The comment at the allocation site records the collector invariant and why this matters for frequent empty varargs binding.

This is a generic XLang3 runtime change. It does not implement or replace `logging.py` in C++; the Python logging module remains Python code.

## Measurements

Official pyperformance 1.14 `--fast` runs and a fresh `--rigorous` run used the same XLang3 Release executable and runner shim. The control and candidate executable SHA256 hashes are the same, so the runtime DLL is the changed component: control DLL `20E542F6186D6406904F223BBA42B0F6E7E072A64A9C3231DBD4F108F290BDAD`; candidate DLL `30DCB6F1E1A189979446FED9278D786F5D2D2C94E388F2BC397F6ADF84037A02`.

| Benchmark | Control | Candidate | `pyperf compare_to` |
| --- | ---: | ---: | --- |
| `logging_silent`, fast pair 1 | 1.43 ± 0.01 μs | 1.40 ± 0.02 μs | Significant, 1.02× faster |
| `logging_silent`, reverse-order fast pair 2 | 1.44 ± 0.02 μs | 1.45 ± 0.08 μs | Noisy; does not favor candidate |
| `logging_silent`, rigorous pair | 1.43 ± 0.01 μs | 1.40 ± 0.01 μs | Significant, 1.02× faster (`t=16.98`) |
| `logging_format`, rigorous pair | 102 ± 4 μs | 99.9 ± 1.6 μs | Significant, 1.02× faster (`t=5.07`) |
| `logging_simple`, rigorous pair | 93.2 ± 1.3 μs | 93.6 ± 3.5 μs | No significant change |

The reverse-order fast pair is a useful counterexample to the first fast result, so the fast-only evidence would not be enough to retain the change. The independent rigorous comparison finds a small, statistically significant `logging_silent` improvement. The broader logging cases show no regression; `logging_format` also improved in the rigorous run, while `logging_simple` was neutral.

Fresh CPython 3.14.7 rigorous measurements were 66.2 ± 0.9 ns for `logging_silent`, 7.19 ± 0.15 μs for `logging_format`, and 6.72 ± 0.09 μs for `logging_simple`. The candidate is still about **21× slower** on `logging_silent`, and about **14× slower** on the other two. This removes one avoidable lock from the hot call path; it does not explain or close the main interpreter overhead gap.

## Validation and artifacts

The full Python fixture suite passed, including [`gc_empty_tuple_tracking.py`](../../tests/fixtures/core/gc_empty_tuple_tracking.py), which checks that empty `*args` and `tuple()` values remain untracked while a tuple containing a list stays tracked. The paired fixed Release regression gate passed all 11 cases; its 21 order-balanced pairs are preserved in [the gate JSON](data/empty-tuple-gc-fixed-baseline-20260930.json).

Raw benchmark files:

- First fast pair: [control](data/empty-tuple-gc-control-fast-20260930.json), [candidate](data/empty-tuple-gc-candidate-fast-20260930.json).
- Reverse-order fast pair: [control](data/empty-tuple-gc-control-repeat-fast-20260930.json), [candidate](data/empty-tuple-gc-candidate-repeat-fast-20260930.json).
- Rigorous XLang3 pair: [control](data/empty-tuple-gc-control-rigorous-20260930.json), [candidate](data/empty-tuple-gc-candidate-rigorous-20260930.json).
- CPython 3.14.7 reference: [rigorous results](data/empty-tuple-gc-cpython314-rigorous-20260930.json).
