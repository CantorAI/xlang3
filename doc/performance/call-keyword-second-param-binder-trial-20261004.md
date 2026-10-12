# Direct binding for a fixed two-slot keyword call — 2026-10-04

The VM now binds a narrow common call shape directly: one positional value and
one keyword naming the second parameter of a two-parameter Python function.
This skips the general binder's scan over the signature and keyword-matching
loop. Expansions, duplicate names, unknown names, and other signatures still
use the generic path. A source comment beside the guard records both the
allocation/scan avoided and the compatibility cases that must remain guarded.
The fixture covers positional-or-keyword and keyword-only second parameters,
plus duplicate and unknown-keyword errors. No Python standard-library code was
reimplemented.

In an isolated 250,000-call loop with the same call shape, a 21-pair
order-balanced Release comparison measured **65.0 ms** for the candidate and
**70.4 ms** for its parent: **0.917× elapsed time** (95% interval 0.913–0.925),
an 8.3% reduction for this binder workload. The raw paired samples are in
[`call-keyword-second-param-micro-20261004.json`](data/call-keyword-second-param-micro-20261004.json).

On the official pyperformance 1.14.0 `async_tree_eager` workload, the fast-mode
candidate measured **2.79 s ± 0.03 s** and the same-source parent measured
**2.80 s ± 0.05 s**. Pyperf reports the difference as not statistically
significant, so this trial does not claim a whole-workload gain. Both remain
about 32× slower than CPython 3.14.7's saved 86.6 ms result. The shape-level
result supports retaining this small binder specialization, but the async-tree
profile's broader CallEx cost needs another optimization.

Raw official benchmark results:

- [`candidate`](data/async-tree-keyword-second-parameter-candidate-fast-20261004.json)
- [`parent control`](data/async-tree-keyword-second-parameter-control-fast-20261004.json)

Both binaries were Release builds and used `C:\Python\Python314` for the
standard library and pyperformance 1.14.0 dependencies. This is targeted
progress only; the full pyperformance speed goal remains open.

The current candidate's complete comparison against CPython 3.14.7 is recorded
in [`the all-97 report`](../pyperformance-xlang3-call-keyword-binder-vs-cpython314-fast-20261004.md).
It attempted all 97 definitions, completed 45, and matched 49 subtests; the
CPython/XLang3 geometric mean was 0.16492×. That aggregate is not evidence of a
gain from this one binder fast path; the full suite remains far from the goal.
