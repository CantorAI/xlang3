# Native Python entry: remove repeated immutable layout work

Status: held source-only design. No engine changes, execution, AST parsing,
build, counters, or performance claim. Parent S8 full validation is live.

The scoped sorted-key wrapper trial is already rejected. Its public
`run_function_value` reuse left every frame/guard/binder operation intact and
saved only construction/destruction of an empty Interpreter fallback map.
The retained original pprint result was 34.4131887 seconds versus the prior F
single body 33.134718 seconds; these unpaired bodies supplied no useful gain.
The four-file wrapper rollback is frozen in
`scoped-entry-rollback-proposed-20261008-provenance.json`. Do not reimplement
that lease. The Oct3 weak module/frame-cache identity trial also was removed
after a noisy 0.6% mean difference; its evidence is
`doc/performance/async-tree-frame-cache-reuse-trial-20261003.md`.

## Confirmed current entry work

`runtime_call_callable` at functional_iterators.cpp:375 calls the existing
public entry with a fresh Interpreter after the accepted trivial/captured-item
shortcuts. Interpreter itself owns only Runtime and fallback globals/version.
`run_function` already uses a TLS depth-separated frame vector. Its completion
guard at vm_loop.cpp:792-811 drops module/closure/metadata/prepared associations
and nulls frame.module/fn; these owning retirements must remain per activation.
Consequently the next callback's frame reset takes the function-change branch
even when the still-live Python key is unchanged.

The function-change branch loads existing cached FunctionExecutionMetadata,
then `XlangVMInstrCacheStorage::reset` (frame.h:199-210) writes a dense
instruction-count IP-to-cache-slot array and repeats the sparse-site mapping
loop. It separately initializes the dense monitoring state and reconstructs
empty owning cache payloads. `reserve_call_args` (frame.h:1137-1146) scans all
static call-argument lists for their maximum on every such entry. These facts
identify repeated immutable work; their contribution to the measured callback
gap is not established. The branch/direct/callback timing is boundary evidence,
not native attribution to these lines.

## One distinct bounded experiment

Extend current FunctionExecutionMetadata with the immutable IP-to-cache-slot
layout and maximum native argument capacity, computed in the existing cold
metadata builder. Change cache storage to borrow that layout only during the
current active frame; use the current frame's execution_metadata owner as the
lifetime anchor. Keep logical instruction count separate from an empty sparse
layout, so zero-cache functions do not need a dense no-slot fill. Native argument
reservation reads the cached scalar rather than scanning call_args.

This changes setup, not callable dispatch: there is no reused Interpreter,
cross-activation frame identity, weak module association, owning instruction
cache persistence, new TLS pool, private frame-append API, or signature/default
cache. The complete existing monitoring-array initialization and owning cache
reset remain in the first experiment. Reusing their mutable state would add a
different semantic obligation and is outside this proposal.

The prepared-function save/restore path needs an additional exact boundary:
reset(frame.h:405-419) moves instr_cache into prepared_functions, whose current
saved state has no execution_metadata owner. Unbind the borrowed layout before
that move. On restore, keep it unbound until the current function metadata has
been acquired, then rebind before indexing or clear_for_pop. The active cache
and every saved cache must be unbound before pool teardown releases metadata
and prepared module states. Do not add a retained metadata owner to saved state
for this experiment. Unbinding is scalar-only and must not retire cache Values.

The layout pointer must be unpublished before the pool guard drops metadata;
never allow stale `size()`/indexing through a finished activation. Reset binds
the new layout only after the existing metadata owner check. Exceptional,
paused, and generator frames retain their existing metadata lifetime. Code
replacement follows the current FunctionObject/module/function selection on
every call and obtains the new function's layout; do not key a layout by an
unowned Function pointer or preserve the old code association after return.

All values remain owned/retired at existing boundaries: arguments, defaults,
closure copies, globals, result/pending error, frame locals/registers and
fallback namespace. Keep ActiveExceptionGuard and the inherited handled-context
seed, current-frame push/pop, published source/activation/IP, signals, profile,
trace, monitoring, inspection refresh and async dispatch on every entry. The
pure Python libraries and ordinary call body remain unchanged.

## Decision and checks before implementation

Root may choose this setup-only trial after the current checkpoint. A durable
public Interpreter proof should execute a nontrivial branching key through the
native callback boundary repeatedly, inspect its actual cached layout/scalar,
and validate callback results/counts with all prior owner/context checks.
Changing __code__, defaults/globals, and the called function between entries
must select current bindings/layout; nested callbacks and a raising key must
retire the original owners and preserve pending/handled exceptions. Trace,
profile and local monitoring must still observe each real entry. Include a
function with no cache sites and one with several native/attribute/call sites.
Do not instantiate private VM templates or add hot counters for this proof.

First measure unchanged branching direct-call and sorted-callback bodies with
the complete key/output checks, then the unchanged original pure-pickle body.
The layout work is a hypothesis until those measurements show a useful gain.
Preserve control and keep the fixed gate/full correctness/official benchmark
requirements for accepting a runtime change.

## CPython comparison

The pinned CPython 3.14.7 native function vector entry uses a small stack array
for up to eight argument references and pushes/initializes a frame using the
current function/code before entering evaluation. That is source evidence for
a compact entry layout, not a claim that CPython avoids all callback entry or
binding work. It does not construct XLang's dense per-IP cache mapping per
callback. Primary source:
https://raw.githubusercontent.com/python/cpython/v3.14.7/Python/ceval.c
(`_PyEval_Vector`, `_PyEvalFramePushAndInit`, lines1858-2000).

## Current source pins

| Path | SHA-256 |
| --- | --- |
| src/internal/xlang3/interpreter.h | d16f9cdcca9fc26ba33d8e4a0889ffe00f338fe6be9fb218223a2c0d67ad9bef |
| src/runtime/functional_iterators.cpp | 4a038c4b5152b87f8fa4d8d83a0f4961b069b8731cd99a4245c74ee5eee21e2e |
| src/executor/xlang_vm/xlang_vm_loop.cpp | edf6c52d37cc85990a1cfd995b769786773e13bdf70bc8908bd52d3d803da30d |
| src/executor/xlang_vm/xlang_frame.h | c144ecfa382778c73b9f4ad3a34a2644dae080afdcde307855cd961623400ff2 |
| src/executor/xlang_vm/xlang_interpreter.cpp | f309acf74a45f5d212d97f0f0b51b65ed7ff9b86a56bbbdf0ae5047125341ab8 |
| src/internal/xlang3/ir.h | b6417aea98ee8169aa551d849492fd2dfdfca6300a6b491fe7cce85c3242f064 |
