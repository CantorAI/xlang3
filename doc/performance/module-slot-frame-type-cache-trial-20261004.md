# Rejected module-slot frame type-cache trial — 2026-10-04

## Result

Rejected. Caching the typed module-global pointer on each VM frame removed a
repeated `value_as_module()` tag/kind check from `LoadModuleSlot` and fused
`LoadModuleAttr`. Four matched Python 3.14.7 `async_tree_eager` runs did not
show a significant change, so the frame field and dispatch plumbing were
removed. The measured opcode cost is real, but this single check is not a
material end-to-end bottleneck by itself.

## Evidence

The earlier instrumented VM profile attributes 373,314 `LoadModuleSlot`
dispatches per extra scaled loop, with 25.38 ms of positive self-time per loop
(67.98 ns per dispatch). This is diagnostic attribution, not an ordinary
Release score. The trial compared the immediate pre-change Release build and
the type-cache candidate in control-candidate-candidate-control order using
pyperformance 1.14.0 fast mode and the Python 3.14.7 benchmark dependencies.

| Run | `async_tree_eager` mean |
|---|---:|
| Control 1 | 2.81 s ± 0.03 s |
| Candidate 1 | 2.82 s ± 0.03 s |
| Candidate 2 | 2.81 s ± 0.03 s |
| Control 2 | 2.83 s ± 0.03 s |

`pyperf compare_to --verbose --table` hid the benchmark as not significant.
There is no supported speedup claim. Removing one repeated type check alone
did not move the roughly 32× asyncio-tree gap enough to register above noise.

## Validation and retained state

- The candidate compiled in Release and passed `xlang3_interpreter_tests.exe`
  plus four Python 3.14 global/module fixtures:
  `globals_locals_identity`, `dynamic_execution_builtins`,
  `module_getattr_call`, and `exec_live_globals`.
- The module-slot C++ test passed on the final rebuilt source after removing
  the candidate.
- The final Release source passed all 11 fixed-baseline cases with seven
  order-balanced pairs and two warmups; the largest candidate/baseline ratio
  was `gc_traversal` at 1.010×, within the 1.10 limit.
- Candidate executable SHA-256:
  `15DCDB0E2AD3C0CCED8DF5C2668F830606617510FC943DE81C06A430BB2A604E`.
- Candidate runtime DLL SHA-256:
  `A46A114FA48CDABF7E2F9DBA63B9F04D4F49662A7B92E5E429A7B223EA103376`.
- Final rebuilt executable SHA-256:
  `D143F4C887432098AB678D6A13777C732D82B1C0824DBB7E547C692077018ACD`.
- Final rebuilt runtime DLL SHA-256:
  `5680F6DB9F741B4B34CDD33B84497219DEF8DA9F271BA8D8C914A6122A653BA3`.

## Raw evidence

The four JSON results and logs use the
`module-slot-frame-global-cache-*20261004` prefix in `data/`.
The final fixed Release gate is
`data/module-slot-cache-final-fixed-baseline-gate-20261004.json`.

The performance objective remains open. The next trial should reduce a larger
measured portion of VM or call-dispatch work than one tag check.
