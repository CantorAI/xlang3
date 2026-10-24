# Runtime-checkable protocol dispatch layers — 2026-10-10

## Result

The current Release build remains about **22× slower** than CPython 3.14.7 on
the official `typing_runtime_protocols` benchmark. A weakref identity probe in
native `_abc` cache lookup produced a small, sub-noise change to the official
result, then was removed during safety review because an unrelated target
could be concurrently cleared while the probe read its pointer. Its score is
preserved as rejected-trial evidence; the fixed Release binary has been
rebuilt without it.

| pyperformance 1.14.0, fast | Mean |
|---|---:|
| CPython 3.14.7 | 124 μs ± 3 μs |
| XLang3, saved immediate parent | 2.80 ms ± 0.03 ms |
| XLang3, current restored source | 2.79 ms ± 0.04 ms |
| XLang3, exact builtin-MRO fast path | 2.78 ms ± 0.03 ms |
| XLang3, weakref identity candidate (rejected) | 2.78 ms ± 0.04 ms |

The restored result was 0.4% below its parent, within run-to-run noise. The
exact builtin-MRO candidate is also effectively unchanged on this benchmark;
it does not count as a `typing_runtime_protocols` win. Current XLang3 is about
22.4× slower than CPython.

## Exact builtin-metaclass MRO fast path

The runtime now handles `isinstance` and `issubclass` directly when the
object's metaclass is exactly builtin `type` and builtin `type` has not been
modified. A class-version guard disables this path after mutation and returns
to ordinary hook dispatch. This is a C++ runtime change in the type-check
dispatcher; it does not change IR generation or execute Python-library code in
native code. Durable comments in the dispatcher explain the guard and fallback.

An eight-pair diagnostic against the saved parent measured candidate/parent
median ratios of 0.52 for direct exact `isinstance`, 0.35 for indirect exact
`isinstance`, 0.52 for direct exact `issubclass`, and 0.31 for indirect exact
`issubclass`. The direct Python-call control was 1.09, consistent with noise.
These focused type-check gains do not transfer to the typing benchmark because
its structural protocol path spends most of its time in other runtime and
attribute operations.

## Layer diagnostic

`benchmarks/diagnostics/typing_protocol_dispatch_layers.py` times matched,
direct call shapes on both runtimes. Each row is the median of five samples of
2,000 calls and includes its loop overhead. These values are diagnostic, not
pyperformance scores.

| Operation | XLang3 | CPython 3.14.7 | XLang3 / CPython |
|---|---:|---:|---:|
| Direct Python identity call | 0.139 ms | 0.049 ms | 2.9× |
| Exact `isinstance` | 1.032 ms | 0.044 ms | 23.7× |
| Runtime protocol, positive structural match | 62.35 ms | 3.02 ms | 20.7× |
| Runtime protocol, negative structural match | 90.81 ms | 3.90 ms | 23.3× |
| Runtime protocol, nominal match | 9.54 ms | 0.43 ms | 22.3× |
| Five-member protocol, negative match | 92.73 ms | 3.68 ms | 25.2× |
| Direct protocol `__instancecheck__` | 52.51 ms | 2.93 ms | 17.9× |
| Direct `typing._abc_instancecheck` | 2.73 ms | 0.22 ms | 12.4× |
| `inspect.getattr_static` | 42.32 ms | 2.28 ms | 18.6× |
| Ordinary `getattr` | 0.772 ms | 0.060 ms | 12.8× |
| Direct weakref dereference | 0.176 ms | 0.039 ms | 4.5× |

The basic Python call is only about 2.9× slower, while the protocol hook,
`inspect.getattr_static`, and native `_abc` operation are much further behind.
This points to repeated attribute and runtime-operation costs inside the
unchanged Python implementations, not one isolated weakref call. The strongest
next target is the runtime's general attribute and Python-hook execution path.

CPython's `typing._ProtocolMeta.__instancecheck__` remains Python code and calls
the C `_abc` cache before its static-attribute fallback. XLang3 must continue to
execute that Python implementation; optimization belongs in its generic VM,
attribute lookup, weakref, and native `_abc` primitives. The rejected native
set-cache experiment illustrates why matching the CPython container shape is
insufficient: it called the weakref registry's locked lookup on every cache
probe.

## Rejected candidate decisions

The weakref identity probe compared the native ref's target pointer without
the registry lock. Although its microdiagnostic moved direct
`_abc_instancecheck` from 2.73 ms to 2.48 ms per 2,000 calls (about 9%), and
the positive structural protocol case from 62.35 ms to 61.18 ms (about 2%),
the official score moved only 0.7%, within noise. More importantly, a
nonmatching weakref can be concurrently invalidated by another runtime thread;
reading its non-atomic pointer without the registry mutex is unsafe. The
helper and its caller have been removed, and the fixed binary is being rebuilt.

The earlier attempt to replace those vectors with `Value::set` was removed
after the official benchmark regressed to 4.28 ms versus 2.97 ms for its
parent, with higher variability. It required a weakref registry lookup for
each candidate and lost more than indexed membership saved. Neither candidate
is retained in the current source.

## Validation and evidence

### Fixed-path follow-up

The current fixed-path Release binary was rerun on the official
`typing_runtime_protocols` benchmark after the layered diagnostic above. One
fast-mode run measured **2.88 ms ± 0.09 ms**. Compared with the saved CPython
3.14.7 reference of **128.1 μs**, this is **0.044× CPython/XLang3** (about
22.5× longer on XLang3). This is one unpaired, fast-mode confirmation, not a
new full-suite result. The fixed executable SHA-256 was
`E684D30F138B77C67B8D1A5BEEB636BBD42907D66D1C675FBF023ECDE26FB45F`.

The same executable now returns a stable public `type.__mro__` tuple, but the
refreshed 20,000-call primitive diagnostic still measures
`inspect._shadowed_dict` at 101.38 ms versus 5.04 ms on CPython (20.1×),
weakref creation/reuse at 18.51 ms versus 0.72 ms (25.8×), and the MRO getter
at 3.83 ms versus 0.67 ms (5.7×). These rows include Python loop and call
overhead and are diagnostic only. The persistent `inspect._shadowed_dict`
and weakref gaps identify distinct runtime paths for profiling; they do not
prove how much either contributes to the official benchmark.

After rebuilding the current working-tree Release source and removing the
temporary Task-resume timers, a second fast-mode run measured
`typing_runtime_protocols` at **2.86 ms ± 0.06 ms**. This remains about
**22.3× longer** than the saved CPython reference. The executable hash was
`9579A6351E8A0BD15FCA401DDC80E91D7238C21EBAEFCCD66B0BB0BA4279F4A6` and the
runtime DLL hash was
`259BC7976F5CAA9FA6360876C0E09ADDA829859A2A6CC2708BBEB4E13011DD28`.

- [Fixed-path official XLang3 result](data/pyperformance-typing-runtime-protocols-current-fixed-20261010-r1.json) and [runner log](data/pyperformance-typing-runtime-protocols-current-fixed-20261010-r1.log).
- [Rebuilt working-tree Release result](data/pyperformance-async-none-typing-current-release-20261010-r1.json) and [runner log](data/pyperformance-async-none-typing-current-release-20261010-r1.log). The runner rejected `async_tree_none` as a benchmark name; only `typing_runtime_protocols` ran successfully.
- [Fixed-path CPython primitive sample](data/typing-protocol-primitives-cpython3147-current-fixed-20261010.json) and [XLang3 sample](data/typing-protocol-primitives-xlang3-current-fixed-20261010.json).

- Full fixture suite and interpreter C++ tests passed with the exact-MRO
  candidate. Fixture log:
  [`isinstance-builtin-mro-fastpath-fixtures-20261010.log`](data/isinstance-builtin-mro-fastpath-fixtures-20261010.log)
- Exact-MRO candidate official result:
  [`isinstance-exact-mro-candidate-typing-runtime-protocols-20261010.json`](data/isinstance-exact-mro-candidate-typing-runtime-protocols-20261010.json)
- Eight-pair focused candidate/parent diagnostic:
  [`isinstance-fast-path-balanced-20261010.json`](data/isinstance-fast-path-balanced-20261010.json)
- Candidate layer diagnostic:
  [`typing-protocol-dispatch-layers-exact-mro-candidate-20261010.csv`](data/typing-protocol-dispatch-layers-exact-mro-candidate-20261010.csv)
- Current official result and saved parent:
  [`typing-protocol-final-restored-xlang3-20261010.json`](data/typing-protocol-final-restored-xlang3-20261010.json),
  [`weakref-call-dispatch-parent-typing-runtime-protocols-20261010.json`](data/weakref-call-dispatch-parent-typing-runtime-protocols-20261010.json)
- Rejected weakref-identity candidate and its paired parent:
  [`abc-weakref-identity-candidate-typing-runtime-protocols-20261010.json`](data/abc-weakref-identity-candidate-typing-runtime-protocols-20261010.json),
  [`abc-weakref-identity-parent-typing-runtime-protocols-20261010.json`](data/abc-weakref-identity-parent-typing-runtime-protocols-20261010.json)
- CPython 3.14.7 result:
  [`weakref-call-dispatch-cpython3147-typing-runtime-protocols-20261010.json`](data/weakref-call-dispatch-cpython3147-typing-runtime-protocols-20261010.json)
- Direct layer measurements:
  [`typing-protocol-dispatch-layers-xlang3-20261010.csv`](data/typing-protocol-dispatch-layers-xlang3-20261010.csv),
  [`typing-protocol-dispatch-layers-cpython3147-20261010.csv`](data/typing-protocol-dispatch-layers-cpython3147-20261010.csv)
- Rejected weakref-set cache trial:
  [`abc-cache-set-candidate-typing-runtime-protocols-20261010.json`](data/abc-cache-set-candidate-typing-runtime-protocols-20261010.json),
  [`abc-cache-set-parent-typing-runtime-protocols-20261010.json`](data/abc-cache-set-parent-typing-runtime-protocols-20261010.json)

All XLang3 measurements used the fixed executable at
`build-repro/main-verify-20261006/Release/xlang3.exe`; no run folder changed.
