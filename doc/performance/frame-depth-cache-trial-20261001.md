# Saved Python frame-depth cache trial (2026-10-01)

This trial tested caching the number of suspended Python frames in the
thread-local runtime-frame state. The proposed hot-path win was replacing the
TLS frame-stack map lookup and a scan of suspended frame states on every Python
function push with a cached integer read. The depth was updated while saving
and restoring frame state, with a nested-stack invariant test.

The candidate was measured against the ordinary September 30 Release build
from the same source revision. The fixed 11-case gate used nine
order-balanced pairs per case after two warmups. All cases passed the gate's
10% regression threshold, but the candidate was **1.007x slower geometrically**
across the gate. Its `function_calls` result was 4.3% slower, with a 95%
paired interval of 0.5–5.1% slower; the remaining cases were near parity. This
does not support keeping extra depth bookkeeping in the frame state, so the
cache and its temporary test were removed.

| Case | Release control | Cached candidate | Candidate / control | 95% interval |
|---|---:|---:|---:|---:|
| `local_slots` | 13.909 ms | 14.006 ms | 1.004x | 1.000–1.010x |
| `scalar_arithmetic` | 8.516 ms | 8.296 ms | 0.977x | 0.951–0.996x |
| `range_for` | 1.182 ms | 1.213 ms | 1.009x | 0.975–1.038x |
| `function_calls` | 0.945 ms | 0.978 ms | 1.043x | 1.005–1.051x |
| `class_construct` | 9.488 ms | 9.548 ms | 0.999x | 0.987–1.016x |
| `list_append` | 0.405 ms | 0.411 ms | 1.015x | 0.972–1.042x |
| `property_access` | 0.904 ms | 0.913 ms | 1.009x | 0.998–1.022x |
| `deepcopy_memo` | 13.641 ms | 13.454 ms | 0.990x | 0.960–0.995x |
| `json_dumps` | 33.521 ms | 33.790 ms | 1.002x | 0.997–1.014x |
| `gc_traversal` | 6.707 ms | 6.979 ms | 1.027x | 0.999–1.045x |
| `subparsers` | 238.913 ms | 239.598 ms | 1.004x | 0.990–1.012x |

The candidate also passed all 11 cases against the preserved September 27
performance baseline. Those larger gains measure the accumulated changes
between September 27 and this source revision; they do not isolate this cache.
The comparison report and accepted-baseline gate output are preserved in
[`frame-depth-cache-vs-release-20261001.json`](data/frame-depth-cache-vs-release-20261001.json)
and [`frame-depth-cache-accepted-gate-20261001.json`](data/frame-depth-cache-accepted-gate-20261001.json).

The next experiments remain focused on the costs supported by the CPython
source comparison and native profiles: generic VM operations and frame
handoff. Prior direct dict lookup, call-site cache, and generic call-fusion
attempts are already recorded in
[`cpython314-vm-comparison-20260930.md`](cpython314-vm-comparison-20260930.md)
and should not be repeated without a materially different design.
