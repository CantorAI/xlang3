# Retain ownerless cache payloads in place: trial background

> Update (2026-10-02): the candidate has now been built and measured. See the
> [completed trial report](ownerless-cache-cleanup-trial-20261002.md) for the
> A/B results, fixed Release gate, and validation status.

This candidate targets generic Python frame-return work. The saved native
profiles attribute about 5% of positive diagnostic self-time to cache cleanup
in pure-Python unpickle and DeltaBlue. Those instrumented shares are useful
for choosing a target; they do not establish an expected benchmark speedup.
See the [frame-cost investigation](native-vm-frame-costs-20260930.md).

The current runtime already preserves selected non-owning attribute and method
guards across activations. On each return, however, it resets the complete
payload and reconstructs the selected scalar fields. The candidate keeps that
record in place when all its owning `Value` fields are invalid and its method
vectors are empty. It uses exactly the existing list of cache kinds permitted
to survive return. Owning payloads still take the original release path;
global payloads and monitoring masks still reset on every pop.

The existing class/layout version guards must remain responsible for mutation
and destroyed-owner invalidation. No pure-Python standard-library code changes.
The preceding cache-storage, cache-counter, and call-site trials are different
designs and are already recorded in the
[CPython VM comparison](cpython314-vm-comparison-20260930.md).

The candidate adds C++ ownership checks for both attribute and method domains,
including fused global owners, every owning active payload field, surviving
guards, and monitoring `DISABLE` masks. Existing Python cache fixtures cover
method mutation, owner release, shape changes, and monitoring restart.
At the time this proposal was written, these tests had not yet run against
the candidate. They pass in the completed trial linked above.

The control executable is preserved in
`scratch/performance/validated-candidate-20261001/` and matches the current
full-suite executable byte for byte:

| File | SHA-256 |
| --- | --- |
| `xlang3.exe` | `07A9B84493AF7A63A0167681044EAF9FCA29683D4FCADCFE1CD9EE6135FFA565` |
| `xlang3_runtime.dll` | `FC938DE91D857AF7EE3599F568A8D32BEEB0F48DCFE903EC2469B263E163CC75` |

This proposal and its original source patch are retained as design history;
use the dated completed trial report for the current implementation status.
