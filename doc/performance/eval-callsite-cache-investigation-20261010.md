# Reused eval code and cold call-site caches — 2026-10-10

The fast-call adapter trial removed argument-vector construction for
`eval(source, globals, locals)`, but the focused `compiled_eval` probe stayed
flat (57.77 ms before and 58.58 ms after per 10,000 calls). The official Genshi
repeat improved against an earlier unpaired run, but that result is not enough
to attribute a speedup to the adapter. See
[the trial report](genshi-eval-fastcall-trial-20261010.md).

Current source inspection identifies a more direct VM target. Genshi reuses
compiled expression code and calls `eval(code, globals, locals)` for every
expression. `builtin_eval` reaches `run_code_object`, which creates a fresh
`Interpreter` and enters the code through `Interpreter::run_module`. The VM's
call-site caches live on frames. `VMFrame::clear_for_pop` clears owning CALL
payloads when a Python frame returns; some ownerless cache fields can survive
ordinary frame returns within the same interpreter activation. The
thread-local frame-pool guard then clears prepared-function state when that
activation ends, and `VMFrame::reset` starts the next activation with a reset
cache vector. Therefore the reused eval code cannot retain its owning Python
call target cache across these evaluations. This is a source-level lifecycle
fact; the size of its timing impact remains unmeasured.

This is a general compiled-code/runtime issue, not a Genshi-specific
optimization. Any remedy must preserve the existing lifetime rule. Current
call-site cache entries may own Python `Value`s; keeping them alive in a
thread-local frame pool can retain functions, globals dictionaries, and module
cycles beyond user-visible lifetimes. A code-owned or weak cache would also
need correct invalidation for function `__code__` replacement, global rebinding,
monitoring/tracing changes, concurrent execution, and garbage collection.

## Bounded cache trial and decision

A prototype retained only sanitized, ownerless `CallMethod` cache entries in a
bounded thread-local store, keyed by weak module ownership and function ID. It
was limited to eval execution and bypassed tracing, profiling, debug stepping,
and sys.monitoring. The complete fixture suite passed with the prototype.

| Official Genshi run | `genshi_text` median | `genshi_xml` median |
|---|---:|---:|
| Inactive-cache control, 20 values | 343.21 ms | 877.02 ms |
| Cache enabled, 20 values | 337.68 ms | 901.34 ms |

The runs were unpaired. Text improved 1.6%; XML regressed 2.8%, with XML
standard deviation rising from 7.25 ms to 15.88 ms. The focused one-pass
`compiled_eval` probe moved from 57.60 ms to 56.91 ms per 10,000 calls, while
`Expression.evaluate` moved from 99.09 ms to 95.86 ms. Those small changes do
not establish a reliable full-render gain. The prototype was removed because
it adds cache-lifetime machinery without a clear overall benefit. Both raw
pyperf files remain available below for future comparison.

The next larger target is the nested interpreter entry itself: `eval` invokes a
fresh `Interpreter` and a separate VM loop for each code object. Reducing that
reentry/frame cost requires a generic execution-continuation path that preserves
live globals/locals, traceback, monitoring, and reentrancy semantics. The cache
trial does not solve that cost.

### Continuation design findings

The existing native callback API is synchronous: `call_native_function`
expects the callback to return a value or exception before it can emit the
matching C-return or C-raise event. It has no result type for requesting a
Python frame push. The natural extension point is the ordinary `Call` opcode
inside `Interpreter::run_function`, where the VM already owns the frame-push
and frame-finish machinery. An opt-in VM-aware native result could represent
either a completed value or an owned Python-frame request; normal native calls
and calls made without an active VM continuation would retain the synchronous
path.

For `eval`, that request must carry the compiled module/function, live globals,
explicit locals, code mode, and ownership of the locals-scope token. A new
frame return mode would place the evaluated value in the caller and preserve
`single` mode's displayhook behavior. The locals token must survive until the
eval frame returns or unwinds, and be released on every exit path. C-call and
C-return/C-raise events must bracket the entire eval frame. Tracebacks,
`sys._getframe`, tracing, profiling, monitoring, exception handlers, and
reentrant eval must continue to observe the correct caller and eval frames.

This remains a design proposal, not an implementation. The likely saving is
removing nested `run_function` dispatch and evaluator-state setup; constructing
the `Interpreter` alone is not the main cost. Before adopting the design, build
a small prototype and compare repeated compiled eval and Genshi workloads,
including observer-enabled cases and the full fixture suite.

## Fixed-path Genshi reference rerun

A fresh pyperformance 1.14.0 fast run against the fixed Release executable
measured `genshi_text` at **333 ± 21 ms** and `genshi_xml` at **1.03 ± 0.19 s**.
The matching CPython **3.14.7** run measured **19.5 ± 0.5 ms** and **43.6 ±
1.1 ms**, respectively. That is a rough 17× text and 24× XML gap, but pyperf
marked both XLang3 results unstable, so these are baseline scale indicators,
not precise ratios. They confirm the nested eval path remains worth testing
against the actual render workloads.

Artifacts: [XLang3 JSON](data/pyperf-genshi-current-parent-xlang3-20261010.json),
[XLang3 log](data/pyperf-genshi-current-parent-xlang3-20261010.log),
[CPython JSON](data/pyperf-genshi-current-parent-cpython3147-20261010.json),
[CPython log](data/pyperf-genshi-current-parent-cpython3147-20261010.log).

## Evidence

- [Pre-adapter Genshi phase probe](data/genshi-runtime-refresh-xlang3-20261010.json)
- [Post-adapter Genshi phase probe](data/genshi-eval-fastcall-xlang3-20261010.json)
- [Pre-adapter native-call counters](data/genshi-runtime-refresh-xlang3-counters-20261010.log)
- [Post-adapter native-call counters](data/genshi-eval-fastcall-xlang3-counters-20261010.log)
- [Inactive-cache Genshi control](data/pyperformance-xlang3-genshi-callcache-trial-20261010.json)
- [Cache-enabled Genshi trial](data/pyperformance-xlang3-genshi-callcache-enabled-20261010.json)
- `src/builtins/functional_builtins.cpp`: `builtin_eval` and `run_code_object`
- `src/executor/xlang_vm/xlang_vm_loop.cpp`: thread-local frame-pool cleanup
- `src/executor/xlang_vm/xlang_frame.h`: `VMFrame::clear_for_pop`
