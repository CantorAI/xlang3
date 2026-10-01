# CPython 3.14.7 VM comparison (2026-09-30)

This comparison checks the actual CPython 3.14.7 evaluator against XLang3's
lowering and native VM for the same standard-library Python code. It is a
source and bytecode diagnosis, not a performance experiment. The benchmark
score remains the separate official pyperformance measurement documented in
the matched pyperf files for [XLang3](data/getitem-const-candidate-rigorous-20260930.json)
and [CPython 3.14.7](data/getitem-const-cpython314-rigorous-20260930.json).

## The workload is the same Python code

The pure-Python pyperformance pickle case runs `pickle._Unpickler`; it does
not select `_pickle.Unpickler`. In CPython 3.14.7,
[`_Unpickler.load`](https://github.com/python/cpython/blob/v3.14.7/Lib/pickle.py#L1192-L1219)
binds `read` and `dispatch` to locals, then executes `dispatch[key[0]](self)`.
XLang3 compiled that same `Lib/pickle.py` from the CPython 3.14.7 installation.
Its IR for this line is:

```text
65: LoadLocalPair  ; dispatch, key
66: GetItemConst   ; key[0], with constant index 0
67: GetItem        ; dispatch[key[0]]
68: LoadLocal      ; self
69: Call           ; handler(self)
70: Pop
71: Jump           ; loop back
```

The full function dump is in
[`xlang3-pickle-unpickler-load-ir-getitem-const-20260930.txt`](data/xlang3-pickle-unpickler-load-ir-getitem-const-20260930.txt),
captured from the current Release executable after constant-subscript fusion.
That executable's SHA-256 is
`5CDE9F716D7464C8E304DCD5963DFE63CD59499128BB6FB837DC5F9E63C16362`.
The earlier
[`xlang3-pickle-unpickler-load-ir-20260930.txt`](data/xlang3-pickle-unpickler-load-ir-20260930.txt)
is the pre-fusion snapshot.
This confirms that the comparison is not explained by a CPython fallback or a
different pickle algorithm. XLang3 already uses indexed locals and a paired
local-load IR instruction. Those indexes remove repeated name lookup, but they
do not make `dispatch[key[0]]` a static function reference: the dispatch table
can change, so the VM still must look up its current entry and invoke that
function with Python semantics. CPython specializes the same dynamic operations
at the bytecode site.

## What CPython does at this hot site

After warming the actual pyperformance workload (protocol 5; `DICT`, `TUPLE`,
and `DICT_GROUP`; 500 rounds of three loads), CPython 3.14.7's adaptive
disassembly at the loop shows:

```text
LOAD_FAST_BORROW_LOAD_FAST_BORROW  (dispatch, key)
LOAD_SMALL_INT                     0
BINARY_OP                          []
BINARY_OP_SUBSCR_DICT              []
LOAD_FAST_BORROW                   self
CALL_PY_EXACT_ARGS                 1
POP_TOP
JUMP_BACKWARD_NO_JIT
```

The warmed disassembly, including inline-cache counters, function version,
workload description, and source hash, is preserved in
[`cpython314-unpickler-load-pyperformance-warmed-dis-20260930.txt`](data/cpython314-unpickler-load-pyperformance-warmed-dis-20260930.txt).
This is a diagnostic view of the benchmark's actual inputs, not a pyperf score.
CPython specializes the dispatch dictionary access and keeps the one-argument
Python call on `CALL_PY_EXACT_ARGS` for the observed workload.

In CPython 3.14.7, the
[`CALL_PY_EXACT_ARGS` macro](https://github.com/python/cpython/blob/v3.14.7/Python/bytecodes.c#L3718-L3727)
checks the function version, exact argument count, available frame-stack space,
and recursion limit. Its frame initialization path
([`_INIT_CALL_PY_EXACT_ARGS`](https://github.com/python/cpython/blob/v3.14.7/Python/bytecodes.c#L3678-L3688))
pushes an `_PyInterpreterFrame` and transfers the argument stack references
into its locals; `_PUSH_FRAME` then changes the active frame and continues the
same evaluator loop
([`_PUSH_FRAME`](https://github.com/python/cpython/blob/v3.14.7/Python/bytecodes.c#L3689-L3704)).
The exact-dict subscription specialization likewise checks for an exact dict
and calls `PyDict_GetItemRef` directly
([`BINARY_OP_SUBSCR_DICT`](https://github.com/python/cpython/blob/v3.14.7/Python/bytecodes.c#L911-L936)).
CPython keeps most of these compact frames contiguous on a per-thread data
stack for locality
([frame design](https://github.com/python/cpython/blob/v3.14.7/InternalDocs/frames.md#L13-L20),
[call handling](https://github.com/python/cpython/blob/v3.14.7/InternalDocs/interpreter.md#L169-L200)).

XLang3 also keeps Python calls on its VM frame stack and returns to the same
opcode loop, so this is not simply “recursive C++ call versus recursive
interpreter.” XLang3 already performs indexed local loads with borrowed object
Values (`LoadLocalPair` uses `value_borrow_assign_fast`), and list/tuple
`GetItem` results use borrowed Values too. A missing local-name lookup or an
avoidable retain on these paths does not explain this loop's gap.

The frame boundary still differs. XLang3 refreshes runtime frame views and
publishes current globals, locals, and frame identity on each switch. Its VM
frames also own per-site cache payloads that must be cleared safely when an
activation returns. The instrumented unpickle profile attributed 10.87% of
positive self-time to VM frame switches; a separate probe measured 5.64% in
cache cleanup, 3.71% in frame reset, and roughly 3.8% in frame-view and
current-frame publication. The instrumented [40-loop attribution data](data/unpickle-native-vm-timing-delta40-20260930.json)
contains the raw and workload-subtracted values. These measurements locate
costs; they are not ordinary-build speedup claims.
## The interpreter loop matters too

CPython and XLang3 both use switch-based instruction dispatch in these builds;
CPython's 3.14.7 bytecode definitions set `USE_COMPUTED_GOTOS` to zero in
[`Python/bytecodes.c`](https://github.com/python/cpython/blob/v3.14.7/Python/bytecodes.c#L40-L49).
Replacing XLang3's switch with computed gotos alone is therefore not supported
by this comparison as the explanation or fix.

The native XLang3 profile attributed 18.75% of positive self-time to loop
control, or 37.1 ns per dispatched IR instruction in that instrumented build.
The loop performs its pending-event poll, deferred-exception and code-end
checks, debug and observation checks, optional counters, yield checks, the
opcode switch, memoryview lifetime cleanup, and instruction-pointer advance.
The event helper keeps its common path to a thread-local countdown and polls
the cross-thread event word every 64 instructions. CPython places its periodic
eval-breaker check in operations such as `CALL`, backward jumps, and `RESUME`
([periodic checks](https://github.com/python/cpython/blob/v3.14.7/Python/bytecodes.c#L147-L164),
[the `CALL` macro](https://github.com/python/cpython/blob/v3.14.7/Python/bytecodes.c#L3533-L3534),
[backward-jump macros](https://github.com/python/cpython/blob/v3.14.7/Python/bytecodes.c#L2686-L2695)).
The XLang3 event-delivery bound is stricter than relying only on those
operation boundaries, so any loop optimization must preserve the 64-IR-op
bound while keeping the common path cheap.

### The per-op event-poll call was already optimized away

The source initially looked like a clear loop cost: the XLang3 dispatcher
calls `interpreter_poll_pending_events()` once per IR instruction, while
CPython keeps its periodic checks inside the evaluator operations. I moved the
existing TLS countdown and acquire-load body into an inline hot-path helper,
leaving the 64-instruction bound, immediate local weakref hint, and shared
atomic state unchanged. A Release/LTCG build produced byte-for-byte identical
`xlang3.exe` and `xlang3_runtime.dll` files for the control and candidate
(executable SHA-256
`FA7053F4318A11BAAAB959F9E49EA3FA4E547B96D8F0929BBF437AF140EC4CCE`;
runtime SHA-256
`0612ABD05DDC41B5B50734E1DBD19CB7ABFA0D0F7419BB87F8C8282FC187595E`).
Thus the source rewrite did not change generated binaries; link-time
optimization already produced the same result.

Two order-balanced rigorous `unpickle_pure_python` comparisons likewise did
not provide a repeatable benefit. With control first, control measured
3.35 ± 0.20 ms and candidate 3.36 ± 0.32 ms; `pyperf compare_to` hid the
difference as insignificant. In the reverse order, candidate measured
3.46 ± 0.40 ms and control 3.33 ± 0.12 ms; pyperf favored control by 1.04×.
Because both pairs used byte-identical binaries, that order-sensitive result
is host variation, not an effect of the source rewrite. The source change was
reverted. Raw data: [control-first](data/event-poll-inline-control-repeat-rigorous-20260930.json),
[candidate second](data/event-poll-inline-candidate-repeat-rigorous-20260930.json),
[candidate-first](data/event-poll-inline-candidate-rigorous-20260930.json),
and [control second](data/event-poll-inline-control-third-rigorous-20260930.json).

This rules out adding an inline wrapper around the event poll as a speedup.
The measured loop-control share still points to larger work: remove actual
per-instruction checks or dispatch overhead, or reduce frame transitions,
with a binary-level change and a matched benchmark gain as the acceptance
criteria.

The same instrumented profile measured 13.84% in `GetItem`, 12.13% in `Call`,
and 10.86% in `CallMethod`. An exact built-in-dict/integer-key shortcut at the
dispatch site was already tested and discarded: it measured only 1.01x faster
and did not materially close the gap ([raw result](data/unpickle-pure-python-exact-dict-int-getitem-fast-xlang3-20260929.json)).
Repeating that lookup-only change is not the next target.

## Why a CPython-like integer side table did not help

CPython 3.14.7's [`dictobject.c`](https://github.com/python/cpython/blob/v3.14.7/Objects/dictobject.c#L15-L45)
documents the compact signed index array beside the insertion-ordered entry
array. Its index width is 8, 16, 32, or 64 bits according to table size, and
the table is resized before it becomes two-thirds full
([load threshold](https://github.com/python/cpython/blob/v3.14.7/Objects/dictobject.c#L481-L492)).
For integer keys, the integer itself is its hash; lookup starts from low hash
bits and uses the `5*i + 1 + perturb` recurrence after collisions
([probe sequence](https://github.com/python/cpython/blob/v3.14.7/Objects/dictobject.c#L917-L957)).
The generic lookup routine reads the compact index, compares the matching
entry, then advances the perturb probe sequence
([`do_lookup`](https://github.com/python/cpython/blob/v3.14.7/Objects/dictobject.c#L917-L957)).
At the bytecode site, CPython's specialized dictionary subscript checks for an
exact dict and calls `PyDict_GetItemRef`
([`BINARY_OP_SUBSCR_DICT`](https://github.com/python/cpython/blob/v3.14.7/Python/bytecodes.c#L911-L936)).
This makes the common integer lookup a cheap first index read while keeping
insertion order in the entries.

XLang3's [`DictObject`](../../src/internal/xlang3/mapping.h) keeps ordered
`Value` key/value entries authoritative and currently maintains separate key
indexes. The trial replaced the integer `unordered_map` with another vector
side table storing the full integer key and an entry index. Although it copied
CPython's integer hash and collision recurrence, it did not copy the compact
index/entry layout: it added a separately allocated table and duplicated key
data. The ordinary official `unpickle_pure_python` rigorous comparison showed
no improvement, and both runs were host-noisy:

| Runtime | Mean | Standard deviation |
|---|---:|---:|
| Parent | 3.44 ms | 0.37 ms |
| Integer side-table candidate | 3.46 ms | 0.37 ms |

Both pyperf runs reported 11% variation. The candidate is rejected and its
runtime code was removed. The raw runs are preserved as the
[candidate](data/integer-index-probe-candidate-rigorous-20260930.json) and
[parent](data/integer-index-probe-parent-rigorous-20260930.json). The tested
candidate executable/runtime hashes were
`92D159F02A071AD625318FB614BD48E5FCA0BDD4AD601C4FC07E1298B3C05D5A` and
`892463924AD78CEC8F957339E2A22136AC363FEAB64448E29508CAA96DDCFDFD`;
the parent pair was
`9F0D28ED78921385DD8763837860B2E2EDB8FF671899E5669E9694CD95FB3BB3` and
`F8AB0AB4D59A9C5082E8C7A639E19C589E96043F3935D80DEB12979EDD479696`.

This source comparison narrows the next work to the larger measured difference:
CPython's warmed `BINARY_OP_SUBSCR_DICT` and `CALL_PY_EXACT_ARGS` paths handle
the dispatch lookup and exact Python call at the bytecode site, while XLang3
still spends substantial time in generic VM `GetItem`, `Call`, frame switching,
and instruction-loop work. The earlier direct integer lookup, call-site, and
dead-argument-transfer trials did not show a repeatable gain. A future change
needs to reduce those shared VM costs and keep the relevant guard/invalidation
rules explicit in the implementation.

## Smaller inline VM frames were also rejected

CPython's [`_PyInterpreterFrame`](https://github.com/python/cpython/blob/v3.14.7/Include/internal/pycore_interpframe.h)
stores slots for that code object's locals-plus and evaluation stack. XLang3's
`XlangVMFrame` instead embeds inline storage for 64 `Value` locals, 64 cells,
and 128 registers, even for a function that uses only a few. The measured
baseline frame size is 4,752 bytes. Reducing those capacities to 8, 8, and 32
would shrink the fixed inline shell by 3,328 bytes (to 1,424 bytes by the same
field-layout calculation); larger buffers still spill to their reusable
vector storage.

The candidate passed the full Python fixture runner and both C++ runtime and
interpreter test executables. Its fast `deepcopy_memo` screen appeared 1.10x
faster, but the rigorous comparison erased that signal: pyperf hid all three
deepcopy subtests as statistically insignificant. `deepcopy_memo` measured
597 ± 75 μs on the parent and 597 ± 61 μs on the candidate; total `deepcopy`
was 4.68 ± 0.54 ms versus 4.75 ± 0.59 ms. Unpickle was likewise inconclusive
in fast mode at 3.51 ± 0.45 ms versus 3.57 ± 0.61 ms. The candidate is
rejected, its capacities were restored, and no full Release regression gate
was run.

The [rigorous parent](data/vm-frame-inline-parent-deepcopy-rigorous-20260930.json)
and [candidate](data/vm-frame-inline-candidate-deepcopy-rigorous-20260930.json)
preserve the subtests that disproved the initial signal. The fast
[deepcopy parent](data/vm-frame-inline-parent-deepcopy-fast-20260930.json),
[deepcopy candidate](data/vm-frame-inline-candidate-deepcopy-fast-20260930.json),
[unpickle parent](data/vm-frame-inline-parent-unpickle-fast-20260930.json), and
[unpickle candidate](data/vm-frame-inline-candidate-unpickle-fast-20260930.json)
preserve the screening runs. Candidate executable/runtime hashes were
`B72D863A730EC5A078DAC3EAFB5E1AA87DCADBC241D5F6224B4667216B5A2CAF` and
`AD39F1DA95AFDA25D4A4B2797754F82EF871282D274C903BAF1933AEFAFCC105`;
the parent pair was
`9F0D28ED78921385DD8763837860B2E2EDB8FF671899E5669E9694CD95FB3BB3` and
`F8AB0AB4D59A9C5082E8C7A639E19C589E96043F3935D80DEB12979EDD479696`.

## JSON calls also use a native XLang3 module

CPython 3.14.7 `json.encoder` imports `_json.make_encoder` and delegates the
built-in container walk to the C encoder when default options apply ([Python
wrapper](https://github.com/python/cpython/blob/v3.14.7/Lib/json/encoder.py),
[`_json.c`](https://github.com/python/cpython/blob/v3.14.7/Modules/_json.c)).
XLang3 registers its own native `_json` at
[`json_module.cpp`](../../src/runtime/modules/system/json_module.cpp). Its
`append_json_builtin` recursively encodes built-in values in native code, while
the standard-library Python `json.dumps`, `JSONEncoder.encode`, and `iterencode`
wrappers call that accelerator. The pure-Python `json` package does not need a
C++ replacement.

The four-input call-path diagnostic isolates the remaining gap. For repeated
empty objects, XLang3's public `json.dumps` costs 10.01 us/call versus CPython's
0.855 us; calling the native encoder directly plus `join` costs 1.005 us versus
0.160 us. For the nested object, public `json.dumps` costs 11.10 us versus
2.719 us, while direct native encoding costs 2.040 us versus 1.823 us. For the
large 1,000-element input, XLang3's native path is faster in that diagnostic:
671 us versus CPython's 1,384 us. The full pyperformance `json_dumps` result is
5.10x slower overall because its repeated small-object calls dominate.

Both runtimes entered the same three Python wrapper functions exactly once per
`json.dumps` call in the frame-count diagnostic. This points to the cost of
executing those Python wrapper instructions in XLang3's VM, plus extra native
call overhead on small objects; it does not indicate a missing native module
or Python fallback. A native-binder shortcut was already measured and rejected
at 1.01x slower, so it should not be repeated.

## Preserve non-owning caches across frame returns

The CPython source comparison exposed a cache-lifetime mismatch: CPython keeps
adaptive guards on the bytecode site, while XLang3 cleared ordinary attribute
and method cache payloads on each frame return to release their owning Values.
The follow-up now retains only non-owning attribute indexes and class-owned
`CallMethod` targets, guarded by globally unique class version tags; all
Value-owning descriptor, property, and generic `Call` caches still reset.
This measured **1.26× faster DeltaBlue** and **1.05–1.10× faster
`unpickle_pure_python`**, with both 11-case Release gates passing. Details,
CPython source links, opposite-order pyperf samples, and the horizontal chart
are in the [cache-lifetime report](vm-inline-cache-cross-activation-20260930.md).

## Current benchmark status and next target

The latest accepted change fuses the constant `key[0]` subscript into one VM
dispatch. In opposite-order official rigorous pairs, XLang3 improved from a
pooled 3.43 ms parent to a 3.31 ms candidate, about **1.04x faster**. A fresh
CPython 3.14.7 run measured 180 μs, leaving this XLang3 candidate **18.36x
longer**, or at **0.054x CPython's speed**. These runs reported sample-jitter
warnings; the two XLang3 pair orders agree on the candidate mean. The matched
raw scores are [XLang3 candidate, first order](data/getitem-const-candidate-rigorous-20260930.json),
[XLang3 candidate, reverse order](data/getitem-const-candidate-repeat-rigorous-20260930.json),
and [CPython](data/getitem-const-cpython314-rigorous-20260930.json). The
[fusion report](constant-subscript-fusion-trial-20260930.md) records parents,
all pair samples, fixture and C++ tests, and the fixed-baseline gate. The
native `BytesIO.read` and `_struct.unpack` adapters help specific calls; they
do not explain most of the remaining gap.

A CPython-guided follow-up fused the warmed `dispatch[key[0]](self)` shape.
Combining the generic XLang3 handlers showed no pyperf gain; adding a guarded
exact-dict-to-Python-function frame-entry path also showed no significant
change. The trial is discarded and its opposite-order data is in
[`pickle-dispatch-call-cpython314-trial-20260930.md`](pickle-dispatch-call-cpython314-trial-20260930.md).

Two call-boundary experiments were measured and rejected. Guarded transfer of
dead positional argument registers measured 3.36 ms for its parent and 3.42 ms
for the candidate. A polymorphic-call-site marker measured 3.41 ms for its
parent and 3.44 ms for the candidate; pyperf classified that 1% difference as
not significant. Neither change is retained. Their raw results are
[`call argument parent`](data/unpickle-pure-python-callarg-transfer-parent-rigorous-20260930.json),
[`call argument candidate`](data/unpickle-pure-python-callarg-transfer-candidate-rigorous-20260930.json),
[`call-site parent`](data/pyperf-callsite-megamorphic-reducedfastpath-parent-rigorous-20260930.json),
and [`call-site candidate`](data/pyperf-callsite-megamorphic-reducedfastpath-candidate-rigorous-20260930.json).

A separate same-module frame-owner trial attempted to avoid shared-pointer
ownership traffic when a reused frame stayed in the same module. It measured
3.49 ms versus the fixed parent's 3.41 ms, or 1.02x slower, so the source
change was removed. The [candidate pyperf result](data/frame-owner-candidate-unpickle-pure-python-rigorous-20260930.json)
preserves that negative result.

A type-based `PolymorphicUserFunction` call-site state modeled after CPython's
function-type call specialization also failed its order-balanced rigorous
check: unpickle improved by only 1% in one order and was insignificant in the
reverse order; DeltaBlue was insignificant in both orders. The candidate was
removed. The [trial report](polymorphic-user-function-call-trial-20260930.md)
preserves the matched samples, binaries, and the earlier fast-screen signal.

The registered native `_json` encoder's printable-ASCII run-append experiment
also failed two opposite-order `json_dumps` checks: one was insignificant and
the reverse order favored the control by 6%. The code and fixture were
removed. See the [trial report](json-ascii-run-append-trial-20261001.md) and
its four raw pyperf files.

The cache-lifetime follow-up validates one CPython-inspired change, but the
overall goal remains open. Its `CallMethod` cache does not cover generic
`Call`, which remains the largest call cost in the unpickle profile; item
access, loop control, and frame switching are still substantial shared-VM
targets. The accepted change and its semantic invalidation rules are recorded
in the [cache-lifetime report](vm-inline-cache-cross-activation-20260930.md).

The completed all-97 pyperformance coverage run is documented in
[`pyperformance-xlang3-vs-cpython314-20260930.md`](pyperformance-xlang3-vs-cpython314-20260930.md).
It matched 35 subtests; only `gc_traversal` favored XLang3 in the directional
fast-mode sample, while the geometric mean was 0.137x CPython/XLang3. The full
status table records 31 completed and 66 failed definitions. Until a candidate
clears a matched pyperf A/B and the fixed Release regression gate, the
performance goal remains open. The focused August comparison is consistent
with this suite result: batching made `local_slots` 1.56x faster than CPython
on that workload, while the all-suite geometric ratio remained 0.122x. That
loop optimization was valuable but applied to one narrow hot path; it did not
speed up the unrelated call, container, generator, and standard-library
workloads. See the [August/current loop comparison and full-suite follow-up](pyperformance-xlang3-vs-cpython314-20260928.md).

### Coroutine `SEND` dispatch is a separate frame-switching gap (2026-10-01)

The saved balanced-PGO full-fast comparison measured `coroutines` at 17.44 ms
on CPython 3.14.7 and 416 ms on XLang3: **0.042x CPython/XLang3**, or about
**23.9x slower** for XLang3. The completed full-fast rerun measured 397 ms on
XLang3 (**0.0439x**, about **22.8x slower**). Its [full 97-case report and
horizontal ratio chart](pyperformance-xlang3-vs-cpython314-full-fast-shim-20261001.md)
record the run, every matched subtest, and each failed or timed-out case. The
official
`bm_coroutines` workload recursively evaluates `fibonacci(25)` with `await`
and repeatedly drives the root coroutine with `.send(None)`. It isolates
coroutine chaining without an event loop or a native extension.

CPython 3.14.7's exact-generator `SEND` path pushes the value directly onto
the saved coroutine frame, links that frame to its awaiting caller, and
continues dispatch with `DISPATCH_INLINED` in the same evaluator
([`Python/bytecodes.c`](https://github.com/python/cpython/blob/v3.14.7/Python/bytecodes.c#L1187-L1255)).
The generic `gen_send_ex2` path also enters `_PyEval_EvalFrame`
([`Objects/genobject.c`](https://github.com/python/cpython/blob/v3.14.7/Objects/genobject.c#L2493-L2612)),
but the hot `SEND` opcode can avoid that API boundary for exact coroutine
objects.

XLang3 lowers `await expr` to an `Await` VM instruction
([`lower.cpp`](../../src/sema/lower.cpp#L6509-L6513)). Its `await_op` handler
copies the awaited `Value`, builds send/result/error temporaries, and calls
`generator_send` for the child coroutine
([`xlang_vm_ops_async.h`](../../src/executor/xlang_vm/ops/xlang_vm_ops_async.h#L34-L110)).
That path constructs an `Interpreter` and invokes `resume_generator`, which
enters `run_function` again for each recursive await
([`generator.cpp`](../../src/runtime/generator.cpp#L347-L399),
[`xlang_interpreter.cpp`](../../src/executor/xlang_vm/xlang_interpreter.cpp#L263-L287)).
Ordinary XLang3 Python calls already push frames onto the active VM stack;
the repeated evaluator entry is specific to `Await` delegating to a coroutine.
The synchronous delegation trampoline is not a shortcut here: it explicitly
excludes coroutine objects ([`generator.cpp`](../../src/runtime/generator.cpp#L236-L249)).

An earlier allocation probe makes the remaining work more specific: its
coroutine workload reduced `Function` allocations from 242,786 to 1 and
`Instance` allocations from 242,786 to 2, while `Generator` allocations stayed
at 242,785. The matching fast sample was still **469 ms** on XLang3 versus
**16.9 ms** on CPython (**27.7x slower**). The [allocation counts](data/coroutines-allocation-diagnostic-20260929.csv),
[XLang3 sample](data/pyperformance-xlang3-coroutines-callframe-fast-20260929.json),
and [CPython sample](data/pyperformance-cpython314-coroutines-current-fast-20260929.json)
show why reducing function-wrapper traffic alone did not close this gap;
CPython also creates coroutine objects, but its exact `SEND` path executes
their frames on the current evaluator stack.

The XLang3 path also rebuilds VM execution storage for each such fresh
coroutine: `run_function` starts a local `std::vector<VMFrame>` and reserves
eight entries ([`xlang_vm_loop.cpp`](../../src/executor/xlang_vm/xlang_vm_loop.cpp#L561-L565),
[`initial frame setup`](../../src/executor/xlang_vm/xlang_vm_loop.cpp#L639-L644));
each new `VMFrame` sizes a per-instruction cache to the function's code length
([`xlang_frame.h`](../../src/executor/xlang_vm/xlang_frame.h#L255-L279)). The
per-depth prepared-frame cache is owned by those VM frames, so it cannot reuse
that storage across the 242,785 independent coroutine resumes in this
workload. This makes repeated frame/cache construction a second concrete cost
to measure alongside evaluator re-entry. The implementation target is
therefore the exact-coroutine `SEND` boundary and its frame storage lifetime,
not another allocation-count-only adjustment.

This source comparison makes inline coroutine-frame switching the leading
candidate for the coroutine and async-tree gaps, but it does not by itself
prove the full timing cause. A retained optimization must keep exact-object
guards and a generic fallback, preserve `.send()` results and exception
propagation, keep monitoring/debug hooks observable, and pass fixture, C++,
and fixed Release regression checks. It must optimize the VM frame path while
leaving pure-Python library implementations in Python.
