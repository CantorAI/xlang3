# GC untrack kind guard screen (2026-10-06)

## Result

Rejected as a suite optimization. Moving the collector's object-kind
eligibility switch into the hot final-release path showed a repeatable small
improvement on `async_tree_none`, but no pooled gain across the selected
benchmarks and a possible `subparsers` regression. The change was removed.
This does not explain or materially close XLang3's gap to CPython 3.14.7.

The hypothesis came from the native async-tree sample, where `release(Value)`
and `gc_untrack_object` appeared among the frequent samples. The trial shared
`gc_kind_may_be_tracked()` between cycle collection and final release, allowing
final release to skip a cross-module untrack call for object kinds that can
never enter the tracked-object index. Tracked objects kept the existing
untrack path. This was a generic XLang3 runtime change; no Python library was
replaced.

## Paired pyperformance screens

Two order-balanced control/candidate pairs ran official pyperformance 1.14.0
`--fast` on CPython 3.14.7's standard library and dependency site. Values below
are XLang3 means; the `pyperf compare_to` outputs are preserved in the raw
JSON comparisons.

| Workload | Control 1 → candidate 1 | Control 2 → candidate 2 | Assessment |
| --- | ---: | ---: | --- |
| `async_tree_none` | 3.59 s → 3.56 s | 3.62 s → 3.55 s | Candidate faster by 1–2% in both pairs |
| `subparsers` | 145 ms → 148 ms | 145 ms → 145 ms | First pair favored control; second was neutral |
| `logging_format` | 89.2 µs → 88.5 µs | 88.7 µs → 88.5 µs | No repeatable difference |
| `logging_silent` | 1.06 µs → 1.05 µs | 1.06 µs → 1.06 µs | No significant difference |
| `logging_simple` | 83.4 µs → 82.9 µs | 83.2 µs → 82.8 µs | No significant difference |
| `pickle_pure_python` | 5.17 ms → 5.11 ms | 5.16 ms → 5.13 ms | First pair favored candidate; second was not significant |

The geometric mean was reported as 1.00× for both pairs. Since async gains
were small and the aggregate was neutral, this is not retained as a performance
change. Candidate runs emitted pyperf stability warnings on some subtests, so
the 1–2% async movement is directional evidence rather than a broad speedup
claim.

## Validation and artifacts

The candidate passed `tests/run_fixtures.py` against the isolated candidate
executable (exit 0). The fixed Release artifacts were not modified; their
SHA-256 hashes remained `A5F5028C15E145EDCE645A5AFC25C11FBCE77F51E882312B1FBE06E63C72A4AF`
for `xlang3.exe` and
`BC1B9C0A8086F7E6FB0C037516DC9C1EEA20427FA887E3AA623714BC5EF5DA8D` for
`xlang3_runtime.dll`.

The scratch candidate output was reused for a follow-up code-path check before
its executable and runtime DLL hashes were recorded. The source experiment and
raw pyperf samples are retained here, but an exact candidate-binary identity
is unavailable; this is an additional reason not to treat the screen as an
accepted performance result.

Raw XLang3 pyperf files:

- [Control, pair 1](data/gc-untrack-kind-control-screen-20261006.json)
- [Candidate, pair 1](data/gc-untrack-kind-candidate-screen-20261006.json)
- [Control, pair 2](data/gc-untrack-kind-control-r2-screen-20261006.json)
- [Candidate, pair 2](data/gc-untrack-kind-candidate-r2-screen-20261006.json)

The next optimization should target a concrete shared interpreter cost with
larger measured weight than this collector call guard.
