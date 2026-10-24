# Runtime protocol case profile — 2026-10-10

The official `typing_runtime_protocols` benchmark contains 65 checks per
iteration: five runtime protocols against thirteen object shapes. This
diagnostic times nine representative shapes directly, using the same benchmark
definitions from pyperformance 1.14, 20,000 warmed checks, and five
order-alternated process samples on XLang3 and CPython **3.14.7**.

| Representative check | XLang3 median | CPython 3.14.7 median | XLang3 / CPython |
|---|---:|---:|---:|
| Nominal single data member | 96.78 ms | 4.13 ms | 23.4× |
| Structural class data member | 97.28 ms | 4.07 ms | 23.9× |
| Structural instance data member | 634.72 ms | 32.03 ms | 19.8× |
| Structural property | 98.25 ms | 4.19 ms | 23.4× |
| Structural missing member | 949.47 ms | 38.05 ms | 25.0× |
| Structural single method | 98.27 ms | 4.01 ms | 24.5× |
| Structural five methods | 97.42 ms | 3.94 ms | 24.7× |
| Structural five instance data members | 9.73 s | 129.47 ms | 75.2× |
| Structural property plus method | 486.46 ms | 3.92 ms | 124.2× |

The large-data and mixed cases are the sharpest targets. CPython 3.14's
`typing._ProtocolMeta.__instancecheck__` remains Python, as required, and calls
`inspect.getattr_static` for each protocol member. That Python helper repeatedly
reads `__mro__`, checks class mappings, calls `_shadowed_dict`, and for instance
members calls `object.__getattribute__(obj, "__dict__")` followed by
`dict.get`. The new focused primitive profile measures the latter two inspect
helpers at roughly 15–18× CPython time. This points to repeated generic runtime
and attribute operations underneath the unchanged Python algorithm; it does
not justify translating `typing` or `inspect` into C++.

## Rejected negative-cache change

A class-version-guarded negative cache was tried for the class-level
`__dict__` descriptor lookup inside `object.__getattribute__`. Five paired
samples did not show a gain: the full `inspect.getattr_static` primitive moved
from 424.06 ms to 431.71 ms, while its ratio to CPython stayed about 17.6×.
`object.__getattribute__(obj, "__dict__")` itself stayed near 19.5 ms per
20,000 calls. The cache field and code were removed. Fixture cases for ordinary,
property-backed, and slotted `__dict__` behavior remain useful semantic
coverage in `object_attribute_hooks.py`.

The [case profile data](data/typing-protocol-case-profile-20261010.json) and
[static-lookup primitive samples](data/inspect-static-primitives-negative-cache-20261010.json)
retain raw timings. The current official pyperformance result remains about
22× slower on `typing_runtime_protocols`; these diagnostics are not official
scores and do not change the full-suite result. The next useful step is to
profile the runtime implementation of the repeated class-mapping and
instance-dictionary operations, not to retry generic `Call` binder caches that
already showed no official gain.
