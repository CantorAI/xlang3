# Native Decimal string conversion trial (2026-10-03)

## Result

Retained in the working tree as a native `_decimal` method fast path. The
official pyperformance 1.14.0 `telco` benchmark improved against a control
that already contained the generator-object cache:

| Build | Mean ± standard deviation |
|---|---:|
| Control, formatter registration disabled | 268 ms ± 32 ms; 263 ms ± 22 ms |
| Candidate, formatter registered | 211 ms ± 5 ms; 242 ms ± 26 ms; 224 ms ± 28 ms |

All three candidate samples were statistically faster than both controls in
`pyperf compare_to`. The control mean is **265.5 ms** and the candidate mean
is **225.7 ms**, a directional **1.18× speedup** (15% lower time). The sample
spread is still material, so the aggregate is not a combined confidence
interval.

Against CPython 3.14.7's saved **5.75 ms** result, the three candidate runs
remain **36.7×, 42.0×, and 39.0× slower**. The formatter closes part of the
gap; it does not solve the wider Decimal or Telco performance problem.

## Why this change

The unchanged `bm_telco` workload calls `print(t, file=outfil)` once per
record, 5,000 times per loop. XLang3's `_decimal` module already routes common
Decimal arithmetic and quantize through native callbacks, but string
conversion still ran `_pydecimal.Decimal.__str__`'s Python digit-placement
logic.

CPython 3.14.7's native `_decimal` implements `dec_str` by obtaining the
current context's capitalization flag and calling `mpd_to_sci_size` directly
([CPython `_decimal.c`, `dec_str`](https://github.com/python/cpython/blob/v3.14.7/Modules/_decimal/_decimal.c#L3168-L3187)).
XLang3 now formats an exact finite Decimal directly from its validated
coefficient and slots. It leaves subclasses, special values, unusual slot
values, and explicit `__str__` arguments on the original Python behavior.
Engineering notation continues through the existing `_pydecimal` method and
is covered by the regression fixture. The implementation lives in XLang3's
native `_decimal` boundary; `decimal.py` and `_pydecimal.py` remain Python.

## Measurement setup and identities

All runs used CPython 3.14.7, pyperformance 1.14.0 `--fast`, the same
dependency site and Windows compatibility shim, and the unchanged executable
path `D:\CantorAI\xlang3\build-repro\Release\xlang3.exe`.

The control executable SHA-256 was
`69C551F01CA5BA4C0F75E16A52D8D900543E11D155698AA69E8BF84F9A7A9312`; its
runtime DLL SHA-256 was
`3F1D84600BA0DDF179A2E1B9BD10693E241400B3E3B1A39A409E0A3BD6C86054`. The
candidate executable hash was unchanged. Candidate runs 1–2 used runtime DLL
SHA-256 `7568430DC5D7272C0094CA5C1889A7B7F7C8B4AC24BF8DDA1C2668AB385093C5`;
candidate run 3 used the final runtime DLL hash
`4616AB3825EC913CEA045CBD392FBE682FB006631786A026D6B127383FD115F6`. Both
candidate DLLs were built from the identical saved source file.

Raw samples: [control run 1](data/decimal-native-string-telco-control-r1-20261003.json), [control run 2](data/decimal-native-string-telco-control-r2-20261003.json), [candidate run 1](data/decimal-native-string-telco-candidate-r1-20261003.json), [candidate run 2](data/decimal-native-string-telco-candidate-r2-20261003.json), and [candidate run 3](data/decimal-native-string-telco-candidate-r3-20261003.json). The corresponding CPython result and full-suite XLang3 baseline are in the [CPython 3.14 comparison data](data/pyperformance-cpython314-clean-release-full-fast-20261002.json) and [XLang3 full-run data](data/pyperformance-xlang3-python314-refcount-release-full-fast-20261003.json).

## Correctness

The new `decimal_native_string` fixture matches CPython 3.14.7 for fixed and
scientific notation, signed zero, special values, context capitalization,
engineering notation, inherited subclasses, and subclass overrides. Existing
`decimal_string_format`, `decimal_native_arithmetic`, and
`decimal_native_quantize` fixtures passed too. The C++ runtime-value and
interpreter tests passed. The full fixture runner still stops at the known
`ctypes_pointer_return` failure because libffi is unavailable.
