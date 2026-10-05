# Native GC cycle-candidate prefilter (2026-10-02)

`collect_isolated_native_instance_component()` examines native-backed
instances whose payload edges are mirrored for cycle collection. It used to
construct reverse adjacency and refcount bookkeeping for every registered
candidate before discovering that no reachable object pointed back to the
instance. A cycle necessarily contains an edge to its root, so the collector
now records that fact while building the candidate graph and returns before
allocating the reverse graph when no such edge exists. A direct self-edge is
still retained for the one-node SCC case.

The rationale is intentionally local to cycle detection: native payload edge
mirrors let `gc.collect()` trace cycles that cross a Python instance and its
native state, while ordinary payload graphs should exit before the additional
cycle-analysis allocations. The existing asyncio accelerator regression
checks both a self-referential Future and a suspended Task/Future cycle in
[`asyncio_accelerator.py`](../../tests/native/asyncio_accelerator.py). A focused
self-referential Future smoke run passed with the rebuilt Release runtime.

## Validation

The preserved-baseline gate ran all 11 cases with 21 order-balanced pairs and
5 warmups. It passed. Candidate time divided by baseline time was 1.004× for
`gc_traversal` (95% paired interval 0.981–1.040; medians 8.48 ms and 8.62 ms),
within the gate's 10% tolerance. The full results are in
[`current-release-gc-early-reject-fixed-baseline-20261002.json`](data/current-release-gc-early-reject-fixed-baseline-20261002.json).
The earlier same-day run reported a 1.66× GC slowdown, but a follow-up before
this edit measured 0.994× and the final full gate measured 1.004×. That spread
shows substantial host noise; the final evidence does not support claiming a
measurable GC speedup from this edit. It does show that the rebuilt candidate
passes the fixed-baseline gate without the previously observed regression.

A current full pyperformance 1.14.0 `--fast` run measured `gc_traversal` at
**1.388 ms ± 0.03 ms**. The saved CPython 3.14.7 full-suite reference is
2.332 ms, making XLang3 about **1.68× faster** in this directional comparison.
The run completed 40 of 97 definitions and found two other marginal wins;
the updated [all-97 report and horizontal chart](pyperformance-xlang3-gc-early-reject-vs-cpython314-full-fast-20261002.md)
contains all scores, failures, and raw evidence. The earlier focused fast run
at 1.68 ± 0.24 ms is retained at
[`pyperformance-xlang3-gc-traversal-early-reject-fast-20261002.json`](data/pyperformance-xlang3-gc-traversal-early-reject-fast-20261002.json).

The overall goal remains open. The full comparison's geometric mean is
0.164× CPython/XLang3 across 42 matched subtests; the largest remaining gaps
include `telco` (46× slower), `pickle_pure_python` (21×), and `subparsers`
(34×). The new full-run artifact should be used as the current comparison
baseline for continued optimization.
