# CPython 3.14.7 VM comparison (2026-09-30)

This comparison checks the actual CPython 3.14.7 evaluator against XLang3's
lowering and native VM for the same standard-library Python code. It is a
source and bytecode diagnosis, not a performance experiment. The benchmark
score remains the separate official pyperformance measurement documented in
the matched pyperf files in [XLang3](data/pyperf-callsite-megamorphic-reducedfastpath-parent-rigorous-20260930.json) and [CPython 3.14.7](data/unpickle-pure-python-bytesio-read-fastcall-rigorous-cpython314-20260929.json).

## The workload is the same Python code

The pure-Python pyperformance pickle case runs `pickle._Unpickler`; it does
not select `_pickle.Unpickler`. In CPython 3.14.7,
[`_Unpickler.load`](https://github.com/python/cpython/blob/v3.14.7/Lib/pickle.py#L1192-L1219)
binds `read` and `dispatch` to locals, then executes `dispatch[key[0]](self)`.
XLang3 compiled that same `Lib/pickle.py` from the CPython 3.14.7 installation.
Its IR for this line is:

```text
65: LoadLocalPair  ; dispatch, key
66: LoadConst      ; index 0
67: GetItem        ; key[0]
68: GetItem        ; dispatch[key[0]]
69: LoadLocal      ; self
70: Call           ; handler(self)
71: Pop
72: Jump           ; loop back
```

The full function dump is in
[`xlang3-pickle-unpickler-load-ir-20260930.txt`](data/xlang3-pickle-unpickler-load-ir-20260930.txt).
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

In CPython's 3.14.7
[`CALL_PY_EXACT_ARGS`](https://github.com/python/cpython/blob/v3.14.7/Python/bytecodes.c#L3662-L3727)
path, the evaluator guards the function version and argument count, pushes an
`_PyInterpreterFrame`, transfers positional arguments into its locals, and
switches the current frame in the same evaluator. The general Python-call path
also pushes a frame and dispatches its code in the existing evaluator.
CPython keeps most of these compact frames contiguous on a per-thread data
stack for locality
([frame design](https://github.com/python/cpython/blob/v3.14.7/InternalDocs/frames.md#L13-L20),
[call handling](https://github.com/python/cpython/blob/v3.14.7/InternalDocs/interpreter.md#L269-L279)).

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
[call and backward-jump macros](https://github.com/python/cpython/blob/v3.14.7/Python/bytecodes.c#L2686-L2695)).
The XLang3 event-delivery bound is stricter than relying only on those
operation boundaries, so any loop optimization must preserve the 64-IR-op
bound while keeping the common path cheap.

The same instrumented profile measured 13.84% in `GetItem`, 12.13% in `Call`,
and 10.86% in `CallMethod`. An exact built-in-dict/integer-key shortcut at the
dispatch site was already tested and discarded: it measured only 1.01x faster
and did not materially close the gap ([raw result](data/unpickle-pure-python-exact-dict-int-getitem-fast-xlang3-20260929.json)).
Repeating that lookup-only change is not the next target.

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

## Current benchmark status and next target

The matched official rigorous result is 3.41 ms for XLang3 versus 160 μs for
CPython 3.14.7: **21.36x longer** for XLang3, or **0.047x speed with CPython at
1.0x**. The CPython reference is the same `unpickle_pure_python` pyperformance
benchmark. The raw scores are [XLang3](data/pyperf-callsite-megamorphic-reducedfastpath-parent-rigorous-20260930.json) and [CPython](data/unpickle-pure-python-bytesio-read-fastcall-rigorous-cpython314-20260929.json).
The native `BytesIO.read` and `_struct.unpack` adapters help specific calls;
they do not explain most of the remaining gap.

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

The comparison narrows the next work to Python call dispatch and frame
switching together with XLang3's ordinary IR loop. It does not yet identify a
validated large speedup. XLang3's per-call analysis and cache ownership remain
hypotheses to measure; neither should be removed without same-source timing
and semantic validation.

The completed all-97 pyperformance coverage run is documented in
[`pyperformance-xlang3-vs-cpython314-20260930.md`](pyperformance-xlang3-vs-cpython314-20260930.md).
It matched 35 subtests; only `gc_traversal` favored XLang3 in the directional
fast-mode sample, while the geometric mean was 0.137x CPython/XLang3. The full
status table records 31 completed and 66 failed definitions. Until a candidate
clears a matched pyperf A/B and the fixed Release regression gate, the
performance goal remains open.
