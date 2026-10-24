# `json_dumps` native binder and call-path investigation (2026-09-30)

The `json_dumps` benchmark is still about **5.5× slower than CPython 3.14**
for the small, repeated inputs used by pyperformance. The C++ `_json` module is
already registered and used as the encoder accelerator, matching CPython's
native-module boundary. `json` and `json.encoder` remain Python code. This
investigation rejected a small `_json.make_encoder` binding shortcut and
measured how much work remains in the Python wrapper and VM.

## Native binder trial: no gain

The candidate returned the cached `_json._default_encoder` directly for the
exact default positional arguments used by `json.dumps`, before building the
bound-argument array and checking encoder options. It changed only XLang3's
CPython-compatible native `_json` module.

With pyperformance 1.14.0 `json_dumps --rigorous`, the candidate measured
**39.7 ±0.7 ms**, versus **39.5 ±0.6 ms** for the unchanged XLang3 control.
`pyperf compare_to` reports the candidate **1.01× slower** than control.
CPython 3.14.7 measured **7.22 ±0.18 ms**; pyperf reports it **5.47× faster**
than the candidate. The shortcut is removed. It did not improve the target and
must not be retained as a claimed optimization.

The complete fixed-baseline 11-case Release gate passed. The nine-pair
candidate-to-control gate was inconclusive overall because of an unrelated
`gc_traversal` measurement; `json_dumps` itself measured **0.9985×** candidate /
control time, with a paired interval of **0.9863–1.0041**. The fixed gate and
the paired report are preserved for audit, not as evidence of a JSON speedup.

## Where the time goes

The [call-path diagnostic](../../benchmarks/diagnostics/json_dumps_callpath_split.py)
uses pyperformance's four JSON payload shapes and repetition counts, with seven
local timed passes. These medians are useful to split the path, but are not
pyperf scores:

| Input | Runtime | `json.dumps` | `.encode` | `iterencode` + join | direct `_json` + join |
| --- | --- | ---: | ---: | ---: | ---: |
| Empty object | XLang3 | 10.010 μs/call | 9.187 μs/call | 6.030 μs/call | 1.005 μs/call |
| Empty object | CPython 3.14 | 0.855 μs/call | 0.719 μs/call | 0.601 μs/call | 0.160 μs/call |
| Nested object | XLang3 | 11.101 μs/call | 10.020 μs/call | 7.091 μs/call | 2.040 μs/call |
| Nested object | CPython 3.14 | 2.719 μs/call | 2.610 μs/call | 2.421 μs/call | 1.823 μs/call |
| Large object | XLang3 | 679.6 μs | 650.8 μs | 651.4 μs | 671.3 μs |
| Large object | CPython 3.14 | 1462.2 μs | 1461.7 μs | 1389.5 μs | 1384.0 μs |

The large one-shot payload is already about **2.15× faster** in this local
diagnostic. The suite's deficit comes from repeatedly encoding small objects:
removing the public wrapper and calling `_json` directly cuts XLang3's empty
object path from 10.010 to 1.005 μs per call. Even then, the direct small calls
remain slower than CPython, so the remaining work is in general VM/native-call
overhead as well as Python wrapper execution; adding another special case in
the `_json` binder is not supported by the measurements.

The [VM counter output](data/json-dumps-vm-counters-20260930.txt) includes
roughly one invocation's imports and setup as well as the benchmark body. It
records 4,001 `CallMethodEx` operations, 4,683 `MakeFunction`, 4,675 `MakeTuple`,
and 4,185 `MakeDict` operations. The function and tuple counts are close to the
4,001 `json.dumps` calls: `JSONEncoder.iterencode` creates its `floatstr`
function and default tuple on the native one-shot path too. This is a promising
generic Python function-creation/VM target, but the counters alone do not prove
that optimizing it will help; the next trial must retain dynamic Python
semantics and show a win in official `json_dumps` pyperformance.

## Follow-up: inspect the repeated wrapper bytecode

I compiled the available Python 3.13 `json/encoder.py` with XLang3's IR dump
to tie the hot counters to their source operations. In `JSONEncoder.iterencode`,
each call creates the `markers = {}` object at line 216 and the nested
`floatstr` function at line 224. In `JSONEncoder.encode`, line 201 constructs
the `(list, tuple)` argument used by `isinstance`. These match the repeated
`MakeDict`, `MakeFunction`, and `MakeTuple` counts in the earlier VM profile.
The encoder source creates these values before it selects the native
`_json.make_encoder` path, so `_json` being active does not remove that Python
wrapper work.

This does not justify skipping those operations: CPython executes the same
source-level steps, and their values can become observable when the native
encoder is disabled or its defaults are customized. A useful optimization
must lower the generic creation/call cost while retaining those semantics, or
prove an exact guarded path where the temporary value cannot escape. The IR
from the current accessible Python 3.13 library is preserved at
[`encoder.ir.txt`](data/json-encoder-ir-probe/encoder.ir.txt); the CPython
3.14.7 adaptive-bytecode comparison and earlier call-path measurements remain
in the analysis above.

## Preserved reports

- [Candidate pyperf JSON](data/json-makeencoder-candidate-rigorous-20260930.json)
- [Control pyperf JSON](data/json-makeencoder-control-rigorous-20260930.json)
- [CPython 3.14 pyperf JSON](data/json-makeencoder-cpython314-rigorous-20260930.json)
- [Fixed-baseline Release gate](data/json-makeencoder-fixed-baseline-20260930.json)
- [Candidate-vs-control Release gate](data/json-makeencoder-vs-control-20260930.json)
- [XLang3 call-path CSV](data/json-dumps-callpath-split-xlang3-20260930.csv)
- [CPython call-path CSV](data/json-dumps-callpath-split-cpython314-20260930.csv)
