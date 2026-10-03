# Decimal binary dispatch cache trial (2026-10-03)

## Result

Rejected. Caching the validated native method state for exact Decimal
`__add__`, `__radd__`, `__mul__`, and `__rmul__` did not produce a repeatable
`telco` speedup. The cache used a globally unique class-version guard and
non-owning pointers, preserving normal descriptor lookup after class mutation.
A tightened version validated the cached state only on misses, but it also
failed to improve the benchmark consistently.

| Run | Control | Candidate | Comparison |
| --- | ---: | ---: | --- |
| 1, initial cache | 215 ms ± 18 ms | 220 ms ± 26 ms | Candidate not significant |
| 2, initial cache | 209 ms ± 6 ms | 206 ms ± 6 ms | Not significant versus the same-order control |
| 3, checks removed on cache hit | 226 ms ± 27 ms | 232 ms ± 24 ms | Not significant |

The distributions were unstable. Candidate direction changed between pairs,
and the final tighter version was slightly slower by mean. This lookup is not
large enough to explain `telco`'s remaining gap; future work should target the
larger shared VM/operator costs instead of another method-map cache.

## Guards and correctness

The trial cached only the four installed base Decimal operation callbacks.
Each cache entry was tied to the exact class pointer and its globally unique
class version. A class mutation invalidated the entry before its non-owning
callback state could be dereferenced; cache misses rechecked the live class
attribute and state. Subclasses and patched descriptors retained generic
Python dispatch.

The Decimal arithmetic, quantize, Decimal string-format, and StringIO dispatch
fixtures passed. The C++ runtime-value and interpreter tests passed. The full
fixture runner stopped at the pre-existing `ctypes_pointer_return` failure
because this Release build has no libffi. The experiment was removed and the
fixed Release executable and DLL were restored.

## Build identities

Control: executable `B70A6A046513883F808F088C43BC64B7BF7C9672728E74205F3B67AAAADA52DA`,
runtime DLL `330BA0B48A931AEF5B927DD0151C0ADF062A9FC5A0C4BC345C51F2BF957DF23F`.

Initial-cache candidate: executable `619C592448F9F00245B5C5B69C1E9DEA92A29F13872933C7D0E8AA7F2271715B`,
runtime DLL `0429760F7FC6E8F51180B7811DAF8F47CBF7E0508376BB8620911F876BCDF64F`.

Tightened candidate: same executable, runtime DLL
`D3EFE7A8097A462F00CEC88431CB089B175A31A15FBA120BAE5D43E1FFB669D8`.
The executable path remained `D:\CantorAI\xlang3\build-repro\Release\xlang3.exe`,
and XLang3 loaded the CPython 3.14.7 standard library from
`C:\Python\Python314\Lib`.

## Raw pyperf results

- Control run 1: [JSON](data/decimal-binary-dispatch-cache-control-r1-20261003.json)
- Initial candidate run 1: [JSON](data/decimal-binary-dispatch-cache-candidate-r1-20261003.json)
- Initial candidate run 2: [JSON](data/decimal-binary-dispatch-cache-candidate-r2-20261003.json)
- Control run 2: [JSON](data/decimal-binary-dispatch-cache-control-r2-20261003.json)
- Tightened candidate run 3: [JSON](data/decimal-binary-dispatch-cache-candidate-r3-20261003.json)
- Control run 3: [JSON](data/decimal-binary-dispatch-cache-control-r3-20261003.json)
