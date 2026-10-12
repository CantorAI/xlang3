# Held design: refresh an inspection snapshot with unchanged owners

This is a source-only next experiment, not an applied optimization or a measured gain. It is distinct from VM instruction-cache persistence, the rejected weak-module frame-cache reuse, and the rejected sorted Interpreter wrapper.

Evidence is the unchanged original pure-Python pickle body: 2,460 protocol-5 dumps with fresh CPython byte/roundtrip parity. The current C5 sample has 190 exclusive leaf observations; exact PE/.pdata + relocation-masked COFF matching identifies five in destruction of PublishedRuntimeFrameView records and two in publish_current_frame_state. These seven locations support investigating snapshot ownership churn; they do not establish an achievable speedup or the proportion of calls eligible for reuse. Inclusive stacks are not added to those counts. The attribution is frozen at SHA256 f13ca5768fc3a55a581ea8a70af924b584666a6b4d9793e750436c58ed5eceab.

## Actual implementation point

src/runtime/runtime.cpp:183 holds g_runtime_frame_registry_mutex, copies RuntimeCurrentFrameState, clears owned_frames and frame_stack, and reconstructs owning Module/global records plus public RuntimeFrameView records. It deliberately retains no arbitrary local, cell, register, closure or instruction-cache owners. The VM suspension callback in src/executor/xlang_vm/xlang_vm_loop.cpp:1264 invokes publication after a frame-stack generation change and every 4,096 suspensions. Runtime::set_current_frame_stack itself only updates thread-local pointers. Keep this publication schedule and mutex unchanged.

## Smallest proposed boundary

Add a same-count fast path before the two clears. Require a nonzero source frame count, existing owned/view vectors of exactly that count, and a complete read-only proof before any update:

* Each source frame has non-null module/global/instruction pointers and a valid owning Module. The old and new shared_ptr have the same pointee and the same control block (owner_before false in both directions).
* Each saved globals Value is an owning Object with the identical raw Object pointer and the same non-borrowed representation as the source. Every saved local_values vector is empty, as the current publisher promises.
* The three Values in published.state (current_globals_module, trace_function, profile_function) are exact unchanged owners, or matching canonical Invalid/None. Compare raw Object pointers; value_is is insufficient because it has semantic equality for distinct Code and Frame objects. Decline scalar or borrowed/custom representations rather than expanding this boundary.

After the complete proof, retain all existing owners, copy only current scalar/pointer state, and refresh every frame's function_id, activation_id and instruction index. Rebuild each RuntimeFrameView in its already allocated slot so module-derived local_names and all optional fields match the existing publisher. The activation or function may change at the same depth; never use equal vector sizes to retain old identities or positions. Restore published.state.frame_stack/count to the owned view array before unlocking.

No owner release, retain, container resize, allocation or callback is needed in this fast path. There must be no partial fast-path update followed by fallback. Depth changes, owner changes, missing pointers and unusual representations use the existing full path verbatim. Do not generalize to prefix retention, resizing or retirement-order changes in this experiment.

## Required correctness and measurement

Use existing public Runtime APIs, not a private VM template or new exported state. A C++ test can publish a caller-supplied RuntimeFrameView twice with the same Module/global owner, different function/IP/activation, and confirm the snapshot exposes the second scalar state. Keep current-frame setup lifetime valid until clearing it. Cover depth growth/shrink, changed module/global owner, absent pointers, live trace/profile changes and eventual old-owner destruction with the existing fallback. A worker-thread sys._current_frames fixture should exercise a blocked native call, nested Python callback and subsequent return; the worker's arbitrary locals must not gain snapshot owners. Retain exception/observer behavior and shutdown clearing.

Run the exact frozen pure-pickle child only after those checks and root serialization; compare attributable trials with preserved current binaries, raw receipts and source pins. An eligibility counter or a one-off instrumentation probe, if requested, must be explicitly diagnostic rather than a timing score. No fixture, build, interpreter, AST or benchmark was run to prepare this design.

## Source pins

src/runtime/runtime.cpp SHA256 67b6383867234d092f479512feb80da9387b3a8c5280701ae87cca77947a9af9.
src/internal/xlang3/runtime.h SHA256 4fa330fcee0c6c3b89e51c19be729bda0d321352f12fc40cc828ee5448ee8940.
Current source inventory SHA256 69aaf03539202bbc7fced4dfe46df5f3a801a6d40b8e0360a68245de6ce71937.
The design remains HELD. No actual source/test files were changed.