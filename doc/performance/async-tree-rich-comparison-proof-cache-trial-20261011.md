# Async tree rich-comparison proof-cache trial — 2026-10-11

## Result

The candidate caches the shape/global/builtin/class proof for one exact
`TimerHandle` slot/double comparison. A native `_heapq` callback has no IR
CALL-site cache, so the existing inliner previously repeated its 12-opcode
analysis and the namespace/descriptor checks on each Python `__lt__` callback.
The cached record contains only scalar generations, a slot index, and a
non-owning pointer; any code, namespace, class, builtin-type, or observer change
falls back to the normal Python function path.

In an isolated build pair differing only in the cached-proof dispatch, a
1.5-million-comparison diagnostic fell from 326.8 ms to 270.8 ms (1.21x faster).
The same official `async_tree_io` workload in pyperformance debug mode changed
from 8.89 s to 8.73 s (1.02x faster). This debug result is directional and the
end-to-end gain is small. The official fast-mode case still exceeded its
300-second cap on the candidate, so this trial does not resolve the async tree
failure or establish a valid fast-mode score.

The focused `native_callback_slot_compare` fixture passed on both isolated
control and candidate builds. The cache remains a narrow optimization lead;
larger per-call, attribute, frame, and ownership costs dominate the workload.

## Evidence

- [Native callback comparison microbenchmark, no proof cache](data/native-callback-slot-compare-no-proof-cache-20261011.json)
- [Native callback comparison microbenchmark, cached proof](data/native-callback-slot-compare-proof-cache-20261011.json)
- [Official pyperformance debug, no proof cache](data/async-tree-io-rich-proof-cache-control-debug-20261011.pyperf.json)
- [Official pyperformance debug, cached proof](data/async-tree-io-rich-proof-cache-candidate-debug-20261011.pyperf.json)
- [Official fast-mode candidate timeout record](data/async-tree-io-rich-proof-cache-candidate-timeout-20261011.txt)
- [Current-source 97-case run log](data/pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r4.log)
- [Current-source 97-case comparison and horizontal chart](pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r4-vs-cpython3147.md)

The paired builds share the exact-dict string GetItem cache and all other
current source changes. Only the call to
`xlang_vm_try_cached_slot_double_compare` was enabled/disabled. The no-proof
control executable SHA-256 is
`9D9A4A7D4FE534417606E818D74F80BD07DFC15BDA3251B50F47BF62E89F9F6A` and its
runtime DLL SHA-256 is
`BA9B7C15EAAC76355F55E6AE90D0B2EE43469D0EE25810C70CC74C0318FB6DC2`.
The cached-proof candidate is the isolated candidate build recorded in the
[dict GetItem trial](dict-string-getitem-site-cache-trial-20261011.md).

Diagnostic samples are not pyperformance scores. The completed 97-case r4 run
has 61 scored definitions and 36 failures/timeouts; it places
`async_tree_none` at 4.99 s versus 229 ms on CPython, while all four fast-mode
async I/O definitions time out. See the linked full report for the complete
status list and chart.
