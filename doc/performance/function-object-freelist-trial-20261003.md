# Function-object freelist trial (2026-10-03)

## Result

Rejected. A bounded thread-local freelist for temporary Python function
objects did not significantly improve the official pyperformance 1.14.0
`json_dumps` case. In rigorous mode the candidate measured **36.4 ms ± 2.7
ms**, versus **37.0 ms ± 2.9 ms** for the control; `pyperf compare_to` hid the
difference as insignificant. The fast runs were similarly inconclusive: the
candidate measured 35.8 ± 1.0 ms and 36.4 ± 4.1 ms, while controls measured
36.3 ± 1.3 ms and 38.6 ± 4.0 ms. This small, noisy shift does not justify
changing function lifetime management.

The pool invalidated weak references before reuse and cleared closures,
defaults, metadata, module references, and function attributes. A
CPython 3.14.7 differential probe confirmed that a dead function's weakref
stayed dead and that a subsequently created function had its own closure,
defaults, and empty `__dict__`. Both C++ runtime-value and interpreter tests
passed. The code was removed after the benchmark failed to show an effect.

## Why this path was tested

The pyperformance `json_dumps` workload repeatedly creates the nested
`floatstr` Python function in the unchanged `json.encoder` wrapper, even when
the native `_json` encoder handles the object graph. Reusing function object
storage could have reduced allocator work without replacing Python library
code. It did not measurably reduce the benchmark score, so function allocation
is not the major source of the remaining JSON slowdown.

## Measurement details

All runs used CPython **3.14.7**, pyperformance 1.14.0, and the fixed runtime
path `D:\CantorAI\xlang3\build-repro\Release\xlang3.exe`. Control hashes:
exe `B70A6A046513883F808F088C43BC64B7BF7C9672728E74205F3B67AAAADA52DA`, DLL
`330BA0B48A931AEF5B927DD0151C0ADF062A9FC5A0C4BC345C51F2BF957DF23F`.
Candidate hashes: exe `B672B360407C172DA92CA5DDEABBF96312226DF49DF82081493C08728BEBE71D`,
DLL `529869F524B56603F1EBCA3ADC9458F99DAC047E8ED9887897FCC2AB7122C587`.

Raw pyperf files:

- [Fast control run 1](data/function-object-freelist-json-dumps-fast-control-20261003.json)
- [Fast control run 2](data/function-object-freelist-json-dumps-fast-control-r2-20261003.json)
- [Fast candidate run 1](data/function-object-freelist-json-dumps-fast-candidate-20261003.json)
- [Fast candidate run 2](data/function-object-freelist-json-dumps-fast-candidate-r2-20261003.json)
- [Rigorous control](data/function-object-freelist-json-dumps-rigorous-control-20261003.json)
- [Rigorous candidate](data/function-object-freelist-json-dumps-rigorous-candidate-20261003.json)

The fixed executable path was restored to the control hashes after the test.
