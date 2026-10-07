# Generic repr formatting checkpoint — 2026-10-07

Runtime-aware repr conversions now correctly quote str-subclass elements inside native containers. Percent formatting on str/bytes and `.format()` reuse XLang3's intrinsic repr/ASCII implementation, preserving custom type dispatch, recursion, user exceptions and non-string-result errors. Python library algorithms remain Python. [Source audit and native CPython references](format-repr-runtime-source-audit-20261007.md).

The VM modulo failure paths now propagate pending Python exceptions before creating a fallback error. This is required for formatting callbacks; the successful immediate numeric modulo fast path is unchanged. Comments preserve the reasons for the intrinsic call, outer-container handling, ASCII conversions and failure-only checks.

## Validation

The new fixture agrees with CPython 3.14.7. Complete fixtures pass: 355 core cases, 11 sections, 3 expected failures. All eight C++/SDK/graph checks pass. The unchanged fixed accepted Release baseline gate passes all 11 cases, with 21 order-balanced paired repeats, 5 warmups, the original 10% threshold, and exit 0. No baseline update or shortened gate was used.

Official pyperformance attempts Chameleon and runtime protocols under the existing 300-second full-definition cap. Chameleon fails and runtime protocols complete; overall exit code 1. Executable, runtime DLL and native hashlib identities match at start/end. Fast-mode stability warnings remain. The two-definition run does not replace the frozen full-97 comparison.

| Case | Saved CPython 3.14.7 | Current XLang3 | CPython / XLang3 speed |
|---|---:|---:|---:|
| typing_runtime_protocols | 0.132 ms | 3.349 ms | 0.0395× |
| chameleon | available in saved reference | failed worker | no score |

Runtime protocols remain 25.3× slower than the saved CPython reference. The preceding compliant Python-hook candidate measured 3.270 ms; no speed gain is claimed here. The saved CPython reference retains its [documented historical provenance limits](data/cpython3147-saved-reference-provenance-audit-20261007.json). The overall goal remains active and unfinished.

## Next verified failure and optimization investigation

Chameleon now compiles its generated template successfully and reaches rendering. Rendering reads `bytes.decode` from the builtin type and raises AttributeError. `bytes_decode_method` and its keyword-aware callback already exist in `bytes_methods.cpp`, and instance method lookup exposes them. `bytes_install_class_methods` omits decode from the builtin class. The next repair is native method exposure/binding, followed by CPython receiver/keyword behavior fixtures and official verification; there is no reason to translate Chameleon's Python code into C++.

The first protocol call profile records Python execution in inspect/typing/ABC paths, but includes the initial lazy import of inspect. Those inclusive times are not representative steady-state costs, and profiling disables some optimizations. Warmed call counts and native/VM diagnostics are needed before choosing the next runtime optimization. Repeated successful checks or imports are not speed measurements.

## Evidence

- [Compiled source identities and correctness](data/format-repr-runtime-validation-20261007.json)
- [Passing fixed gate](data/release-format-repr-runtime-fixed-gate-20261007.json) and [gate log](data/release-format-repr-runtime-fixed-gate-20261007.log)
- [Raw official timings](data/pyperformance-xlang3-format-repr-runtime-targeted-fast-20261007.json), [full official log](data/pyperformance-xlang3-format-repr-runtime-targeted-fast-20261007.log), [run provenance](data/pyperformance-xlang3-format-repr-runtime-targeted-fast-20261007-provenance.json), [comparison inputs and hashes](data/format-repr-runtime-official-comparison-20261007.json)
- [CPython fixture reference](data/format-repr-runtime-cpython3147-reference-20261007.json), [successful XLang3 fixture](data/format-repr-runtime-xlang3-fixture-r2-20261007.log), [initial failure that exposed exception propagation](data/format-repr-runtime-xlang3-fixture-20261007.log)
- [Preserved preceding Release](data/percent-repr-preserved-control-20261007.json), [first Python call profile](data/typing-runtime-protocols-python-call-profile-xlang3-20261007.log)
