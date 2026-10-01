# CPython-guided pickle dispatch and call trial (2026-09-30)

## Finding

The slow pure-Python unpickler loop is executing the same `pickle.py` algorithm
as CPython 3.14.7. In the pyperformance workload, its hot expression is
`dispatch[key[0]](self)`. XLang3 already stores local variables in indexed
slots, so local-name lookup is not the missing optimization. The expensive
work is the dynamic dictionary lookup and Python-function call that follow.

CPython 3.14.7's warmed bytecode for this expression uses
`LOAD_FAST_BORROW_LOAD_FAST_BORROW`, `BINARY_OP_SUBSCR_DICT`, and
`CALL_PY_EXACT_ARGS`. The corresponding definitions in CPython's
[`Python/bytecodes.c`](https://github.com/python/cpython/blob/v3.14.7/Python/bytecodes.c)
show the key differences: the dict specialization guards an exact dict and
calls its internal lookup directly; the exact-argument call checks a Python
function's arity and frame capacity, then pushes a Python frame directly.
Borrowed local loads also avoid ownership traffic when the current frame keeps
the value alive. See the saved warmed disassembly in
[`cpython314-unpickler-load-pyperformance-warmed-dis-20260930.txt`](data/cpython314-unpickler-load-pyperformance-warmed-dis-20260930.txt)
and the CPython-to-XLang3 source/IR comparison in
[`cpython314-vm-comparison-20260930.md`](cpython314-vm-comparison-20260930.md).

I tested two XLang3 fusions while leaving the CPython `pickle.py` library code
unchanged. The first combined XLang3 `GetItem`, `LoadLocal`, and generic
`Call` in one VM dispatch. It did not improve the pyperformance result because
it still ran the generic lookup and call handlers. The second retained exact
guards for `dict[int] -> Python function`, looked up the current callable, and
entered it through XLang3's ordinary frame-push helper. It passed the complete
fixture suite and 11-case Release gate, but the official pyperf comparison did
not find a significant gain. This implementation is therefore not retained.
The candidate lowering did create one `GetItemCallLocal` IR operation while
keeping the ordinary `LoadLocal; Call` instructions directly after it as the
fallback; the captured function is
[`xlang3-pickle-unpickler-load-ir-getitem-call-local-20260930.txt`](data/xlang3-pickle-unpickler-load-ir-getitem-call-local-20260930.txt).

The result is consistent with the broader profile: making this one expression
look more like CPython is insufficient while the surrounding XLang3 operation
handlers, VM loop, and frame transitions still account for substantial time.
The instrumented unpickle profile attributes 18.75% of positive self-time to
VM loop control, 13.84% to `GetItem`, 12.13% to `Call`, and 10.87% to frame
switches. Those percentages are diagnostic and should guide the next
optimization, not be reported as normal-build performance scores.

## Official pyperformance result

Both variants used the unmodified pyperformance 1.14.0
`bm_pickle/run_benchmark.py` and pyperf 2.10.0 with
`--pure-python --protocol 5 unpickle`. The pyperf manager was CPython 3.14.7;
the measured XLang3 worker was the parent or candidate Release executable.
The direct-call candidate was measured in opposite orders against the same
parent build:

| Order | Runtime | Mean ± standard deviation | pyperf comparison |
| --- | --- | ---: | --- |
| Parent, then candidate | Parent | 3.35 ±0.32 ms | — |
| Parent, then candidate | Direct-call candidate | 3.28 ±0.37 ms | Not significant |
| Candidate, then parent | Direct-call candidate | 3.32 ±0.32 ms | — |
| Candidate, then parent | Parent | 3.30 ±0.17 ms | Not significant |

Both pairs reported high host jitter. The paired means change direction with
run order; pooling the two means suggests less than a 1% difference, which is
below the noise and not evidence of a speedup. The generic-dispatch fusion
also showed no significant change: its first-order means were 3.36 ms for both
parent and candidate, and its reverse-order means were 3.34 ms for the parent
and 3.45 ms for the candidate.

The fresh CPython 3.14.7 reference for this same case was 0.180 ms. The direct
candidate therefore remained about **18.3× slower**, at roughly **0.055×
CPython's speed**.

```text
unpickle_pure_python elapsed time (shorter is faster; bars run left to right)
CPython 3.14.7       0.180 ms |█
XLang3 parent        3.325 ms |███████████████████
XLang3 trial         3.300 ms |███████████████████
```

## Validation and saved results

The direct-call candidate passed `xlang3_ir_codec_tests`,
`xlang3_interpreter_tests`, `xlang3_runtime_value_tests`, the complete Python
fixture runner, and all 11 cases in the fixed Release regression gate. The
gate report is retained at
[`getitem-call-local-direct-call-fixed-baseline-20260930.json`](data/getitem-call-local-direct-call-fixed-baseline-20260930.json).
The candidate was discarded after pyperf found no significant gain.

| Artifact | SHA-256 |
| --- | --- |
| Candidate `xlang3.exe` | `A5803F11F365F8060E249A030818628BE5D23E81B9217772331A78788A4F5D66` |
| Direct-call candidate `xlang3_runtime.dll` | `1CBDEC4FD667B4BA62AF5ABC3F941DA46809CF56B42B153B4869C2736E2F92B8` |
| Parent `xlang3_runtime.dll` | `0950F854D9BED91A8D3547882AB6B7B148141613A81DD67BFFD38CA1671D9AD6` |

Raw rigorous pyperf files:

- Generic-dispatch fusion: [parent, first order](data/getitem-call-local-parent-first-rigorous-20260930.json), [candidate, first order](data/getitem-call-local-candidate-first-rigorous-20260930.json), [candidate, reverse order](data/getitem-call-local-candidate-repeat-rigorous-20260930.json), and [parent, reverse order](data/getitem-call-local-parent-repeat-rigorous-20260930.json).
- Direct-call fusion: [parent, first order](data/getitem-call-local-direct-parent-first-rigorous-20260930.json), [candidate, first order](data/getitem-call-local-direct-candidate-first-rigorous-20260930.json), [candidate, reverse order](data/getitem-call-local-direct-candidate-repeat-rigorous-20260930.json), and [parent, reverse order](data/getitem-call-local-direct-parent-repeat-rigorous-20260930.json).
- Existing fresh [CPython 3.14.7 reference](data/getitem-const-cpython314-rigorous-20260930.json).
