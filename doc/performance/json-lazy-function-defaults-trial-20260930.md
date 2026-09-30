# Lazy positional-default introspection for Python functions (2026-09-30)

`json_dumps` repeatedly creates a nested Python `floatstr` function in
`JSONEncoder.iterencode`. Each `MakeFunction` copied positional defaults into a
second vector used only by the function object's `__defaults__` attribute;
normal argument binding already uses the primary defaults vector. The runtime
now defers that duplicate vector until `__defaults__` is inspected. This is a
generic function-object allocation change: `json` and `json.encoder` remain
Python, and the existing native `_json` module remains the CPython-native
accelerator.

The code comment in `src/runtime/value.cpp` records why the duplicate vector is
deferred. The attribute path derives the positional tuple from the function's
signature and call defaults when it is actually requested. Functions without
compiled-module metadata retain the old stored representation, and assignment
to `__defaults__` continues to use the existing setter and call-binding data.
The fixture suite exercises original defaults, positional/keyword-only
defaults, and reassignment to both a tuple and `None`.

## Measurements

Official pyperformance 1.14.0 `json_dumps --rigorous` on Windows x64 measured:

| Runtime | Mean | Compared with candidate |
| --- | ---: | ---: |
| XLang3 control | 39.7 ±0.5 ms | Candidate is 1.02× faster |
| XLang3 candidate | 38.8 ±0.5 ms | — |
| CPython 3.14.7 | 7.22 ±0.18 ms | 5.38× faster |

The fixed-baseline Release gate passed all 11 cases. A separate complete
candidate-to-control Release gate also passed all 11 cases; its paired
`json_dumps` median was **38.94 ms** versus **39.87 ms**, a candidate/control
time ratio of **0.980** (95% interval **0.973–0.987**). This is a repeatable
small gain, not a resolution of the remaining CPython gap.

The seven-pass call-path diagnostic is not a pyperf score. On the repeated
empty-object input, XLang3 moved from **10.010 to 9.621 μs/call** for
`json.dumps`, **9.187 to 8.502 μs/call** for `.encode`, and **6.030 to 5.664
μs/call** for `iterencode` plus `join`. Direct `_json` encoding stayed near
**1.0 μs/call**. The same candidate path measured **10.791 μs/call** for the
nested payload and **671.8 μs** for the large payload; the large-output result
is noisy and does not establish a change. The small repeated cases match the
official benchmark's shape and show the saved work is above the native encoder.

## Validation and source boundary

The full Python fixture suite passed, including function metadata/default
checks. The complete 11-case Release comparison passed against both the
preserved fixed baseline and the pre-change executable. No standard-library
source was replaced or reimplemented in C++. The broader CTest run passed 52
of 53 tests; its PowerShell fixture runner mismatched UTF-8 JSON output against
mojibake expected text. The same mismatch reproduces with the saved pre-change
control executable, while the UTF-8-aware Python fixture runner passes.

The preceding `_json.make_encoder` binder shortcut was removed because official
pyperformance found no gain; its negative result is recorded in the
[native binder investigation](json-dumps-native-binder-trial-20260930.md).
This change instead reduces generic VM-created Python function metadata and
was retained because the official target benchmark and paired Release gate
both improved.

Raw evidence:

- Official pyperf: [control](data/json-lazy-positional-defaults-control-rigorous-20260930.json), [candidate](data/json-lazy-positional-defaults-candidate-rigorous-20260930.json), [CPython 3.14.7](data/json-makeencoder-cpython314-rigorous-20260930.json)
- Release gates: [fixed baseline](data/lazy-positional-defaults-fixed-baseline-20260930.json), [pre-change control](data/lazy-positional-defaults-vs-control-20260930.json)
- Call-path CSVs: [pre-change XLang3](data/json-dumps-callpath-split-xlang3-20260930.csv), [candidate XLang3](data/json-dumps-callpath-split-lazy-defaults-xlang3-20260930.csv), [CPython 3.14](data/json-dumps-callpath-split-cpython314-20260930.csv)
- VM counters: [raw output](data/json-dumps-vm-counters-20260930.txt), [decoder](../../benchmarks/diagnostics/decode_vm_counters.py)
