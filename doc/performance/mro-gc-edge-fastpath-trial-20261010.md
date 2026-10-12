# Public MRO tuple cache and GC edge cost — 2026-10-10

The current working source caches the public `type.__mro__` tuple so repeated
reads preserve CPython's identity semantics. This targets repeated MRO reads in
`typing_runtime_protocols` and `sqlalchemy_imperative`; it does not remove
their Python algorithms or make their total runtime close to CPython yet.

The saved official pyperformance run used XLang3 Release and CPython **3.14.7**
with 20 values per benchmark. `sqlalchemy_imperative` measured **221.29 ±
1.72 ms** on XLang3 and **11.42 ± 0.16 ms** on CPython, about **19.4× slower**.
The prior full-suite capture measured 252.7 ms on XLang3, so the new sample is
directionally about 14% faster, but those runs are unpaired and do not prove
the cache caused that entire difference. `typing_runtime_protocols` measured
3.43 ± 1.50 ms on XLang3 and 0.127 ± 0.004 ms on CPython; pyperf marked the
XLang3 result unstable. The MRO cache therefore has a specific promising
SQLAlchemy signal, while the protocol result needs a stable rerun.

The owning tuple creates a `Class -> MRO tuple -> Class` cycle. The GC visitor
must account for that edge. The initial cache trial's fixed-baseline gate was
inconclusive around `gc_traversal`; a follow-up changed only the class edge
visit to use the field's owning-Value invariant directly, avoiding the generic
borrowed-reference check while still visiting the tuple. The comment beside
that visit records why the shortcut is safe and why the edge cannot be omitted.

The direct edge visit passed the focused 21-pair `gc_traversal` comparison
against the preserved immediate parent at **0.995×** candidate/parent time.
Its full immediate-parent gate remained inconclusive because `list_append`'s
interval crossed the threshold; `gc_traversal` itself passed at 1.041×. The
full fixed-baseline gate also remained inconclusive: the GC confirmation was
1.155× with a 95% interval of 1.113–1.174. This is a cumulative candidate vs.
the older fixed baseline and cannot be attributed solely to the MRO cache.

The unchanged official `gc_traversal` benchmark measured **2.44 ± 0.13 ms** on
the candidate and **2.48 ± 0.12 ms** on the preserved immediate parent. Both
fast-mode samples were marked unstable; treat the roughly 1.7% difference as
directional only. The edge visit does not explain the broader slowdown, and
this trial does not establish a suite-wide win.

## Evidence

- [Public MRO cache XLang3 pyperformance JSON](data/public-mro-tuple-cache-r1-xlang3-pyperformance-20261010.json)
- [Public MRO cache CPython 3.14.7 pyperformance JSON](data/public-mro-tuple-cache-r1-cpython3147-pyperformance-20261010.json)
- [Focused GC gate](data/mro-gc-edge-fastpath-r1-20261010.json)
- [Full fixed-baseline gate, 21 pairs](data/mro-gc-edge-fastpath-r1-fixed-gate-20261010.json)
- [Full fixed-baseline gate, 31 pairs](data/mro-gc-edge-fastpath-r2-fixed-gate-20261010.json)
- [Full immediate-parent gate](data/mro-gc-edge-fastpath-r2-parent-gate-20261010.json)
- [Official GC candidate JSON](data/mro-gc-edge-fastpath-r2-pyperformance-xlang3-20261010.json) and [parent JSON](data/mro-gc-edge-fastpath-r2-pyperformance-parent-20261010.json)
- [Release build log](data/build-public-mro-tuple-cache-r1-Release-20261010.log)
- CPython 3.14.7 fixture suite passed after the direct edge visit.

The next worthwhile protocol target is the cost around each weak reference.
Current measurements show weakref creation and dereference far slower than
CPython even after indexing removes registry-wide scans. The previous
single-reference reuse shortcut was rejected because it did not establish a
gain; do not retry it without a new measured hypothesis.
