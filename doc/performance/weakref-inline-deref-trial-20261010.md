# Inline referent lookup for built-in weak references — 2026-10-10

Exact built-in `weakref.ref` dereferences previously took a mutex-protected
hash lookup in `weakref_target_by_reference` before retaining their target.
The runtime now stores the borrowed target pointer in the weakref instance's
native-data fields for the exact built-in `ReferenceType`. `weakref_get_target`
uses that pointer under the same registry mutex and keeps the existing
retain-if-alive check. Target invalidation clears the pointer while holding
that lock, before freeing the target. Enumeration, callback order, and the
map fallback for subclasses or occupied native state remain unchanged. The
source comment records these constraints beside the fast path.

## Results

The focused probe created 20,000 refs to distinct live targets, then timed one
deref per ref. Across five order-rotated process samples, median dereference
time fell from **9.245 ms** on the preserved parent to **7.872 ms** on the
candidate, a directional **14.9% improvement**. Creation medians were 33.80 ms
and 32.49 ms respectively; the difference is within observed run-to-run noise,
so this change makes no creation-speed claim. CPython 3.14.7 medians were
0.595 ms for dereferences and 1.852 ms for creation.

The official fast-mode `typing_runtime_protocols` result was **3.10 ± 0.28 ms**
on the candidate and **3.25 ± 0.71 ms** on the parent. Both XLang3 measurements
were noisy; the roughly 4.6% directional case gain is not a stable claim. A
same-loop CPython 3.14.7 run measured **136 ± 7 µs**, so the candidate remains
about **22.8× slower** on this benchmark. This optimization removes one cost;
it does not explain or solve most of the protocol benchmark gap.

## Validation and limits

The fixed Release build succeeded in the established
`build-repro/main-verify-20261006/Release` path, and the complete Python 3.14.7
fixture suite passed. The complete 11-case regression suite passed against
the preserved immediate parent. The fixed accepted Release baseline gate was
**inconclusive** only for `gc_traversal` (1.150×, 95% interval 1.138–1.171);
that cumulative slowdown is present against the immediate parent as well, for
which `gc_traversal` measured 0.999× (95% interval 0.985–1.014). Therefore the
change passes the incremental parent gate, but the fixed-baseline gate is not
green and this is not a commit-ready performance validation.

This is a runtime change, not an IR optimization. The primitive improvement
is bounded by the remaining mutex acquisition and target-retain operation,
plus Python call/VM work around `ref()`. The official case result confirms
that more protocol-specific costs remain. The full pyperformance checkpoint
is still 75/97 completed, with XLang3's scored geometric mean at 0.12306×
CPython 3.14.7; this targeted experiment does not change that score.

## Evidence

- Primitive timings, sample lists, hashes, and official-run links:
  [raw measurement record](data/weakref-inline-deref-microbench-20261010.json)
- Official pyperformance output:
  [candidate](data/weakref-inline-deref-candidate-typing-runtime-protocols-20261010.json),
  [parent](data/weakref-inline-deref-parent-typing-runtime-protocols-20261010.json),
  [CPython 3.14.7](data/weakref-inline-deref-cpython3147-typing-runtime-protocols-20261010.json)
- Regression gates:
  [complete immediate-parent gate](data/weakref-inline-deref-parent-gate-r1-20261010.json),
  [focused immediate-parent GC gate](data/weakref-inline-deref-parent-gc-gate-r1-20261010.json),
  [complete fixed-baseline gate](data/weakref-inline-deref-fixed-gate-r1-20261010.json)
