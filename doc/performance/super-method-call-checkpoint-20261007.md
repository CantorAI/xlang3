# Immediate super method calls without bound-method allocation — 2026-10-07

Official Chameleon time falls nominally from **168.822 to 158.564 ms**, a **6.08% reduction / 1.065× previous-candidate speed**. Relative to the first completed 178.068 ms run, the two shared super improvements reduce time by **10.95%**, or **1.123× speed**. Chameleon remains **13.38× slower than the saved CPython 3.14.7 reference**. The overall performance goal remains unfinished.

## Guarded shared runtime path

Immediate positional `super.method(...)` calls obtain a plain Python/native function and its receiver without allocating a `BoundMethod`. Both values are retained across the call, including Python re-entry. Native and Python execution still use the existing call dispatcher, preserving original Python frames, profiling events and exceptions. [CPython 3.14.7's native super lookup](https://github.com/python/cpython/blob/v3.14.7/Objects/typeobject.c#L11146) likewise has a method-lookup mode distinct from fetching a bound attribute.

The implementation factors the existing MRO selection into one helper shared with normal attribute lookup. It resolves the current MRO on every call and caches no result across class mutation. Only class-sourced plain Python functions and native functions marked as descriptors qualify, on instance/file receivers. Instance-stored callables, properties, arbitrary descriptors, class/static methods, class receivers, `__new__`, special super attributes and expanded/keyword calls retain ordinary lookup. Saved attributes still construct normal bound methods. Comments explain guards, ownership and observable execution requirements.

No Chameleon or pure-Python library algorithm was implemented in C++.

## Correctness and fixed gate

All **361 core fixtures, 11 section fixtures and three expected-failure checks** pass. All **eight C++/SDK/graph checks** pass. The new fixture passes CPython 3.14.7, the preserved preceding Release and the candidate. It covers native/Python calls, base mutation, diamond MRO order, saved callable identity, static/class methods, keyword/starred fallback, Python/native profile events and original error tracebacks.

A temporary dictionary receiver also survives a custom hash hook that collects garbage and replaces a base method while the native call is in progress. The already-selected native callable completes normally; subsequent calls observe the new method. This validates retention across re-entry rather than relying only on successful primitive lookups.

The first profile assertion assumed CPython's callable event payload. The preserved XLang3 uses a native function-name string instead; the final fixture explicitly recognizes that existing payload. This checkpoint preserves native events but does not claim to repair that profiling API difference or establish complete super ABI compatibility. The separate invalid-hierarchy validation gap recorded in the [preceding checkpoint](super-live-frame-receiver-checkpoint-20261007.md) remains unchanged.

All **11 default fixed-gate cases** pass, with **21 paired repeats, five warmups, the unchanged 10% threshold and exit 0**. The accepted baseline remains `build-repro/Release/xlang3.exe`, and the candidate remains `build-repro/main-verify-20261006/Release/xlang3.exe`. The preceding verified Release is preserved separately. Build and benchmark runs are sequential.

## Official measurement and allocation evidence

| Runtime/checkpoint | Chameleon mean | Speed with CPython = 1× |
|---|---:|---:|
| Saved CPython 3.14.7 | 11.851 ms | 1.0000× |
| First completed XLang3 run | 178.068 ms | 0.06655× |
| Live-frame receiver checkpoint | 168.822 ms | 0.07020× |
| This candidate | 158.564 ms | 0.07474× |

Each official result has 20 timed values. These are nominal fast-mode comparisons; stability warnings remain. The [saved CPython reference has historical binary provenance limits](data/cpython3147-saved-reference-provenance-audit-20261007.json). This selected run is not a fresh full-suite report and must not be spliced into the frozen 97-definition data.

The candidate's separate untimed render retains **222,553 characters** and UTF-8 SHA-256 `ee20adc6250db78d5443e8d50cc9e940f448151dab8ce51e5d83aea93531616c`, matching the preceding CPython render. Executable, runtime DLL and native hashlib hashes remain unchanged at official run start/end.

An instrumented diagnostic removes **3,094 bound-method allocations** across its complete process: 5,866 in the preserved control versus 2,772 in the candidate. Both record **16,813 native calls**. These counters confirm optimization eligibility; the diagnostic also includes imports, warmups and other operations, so its whole-process delta is not attributed solely to one source expression. Counter-enabled timing values are not used as performance scores.

An uninstrumented five-sample diagnostic measures 20,000 zero-argument-super forwarding calls at 31.138 ms, versus 32.435 ms in the preceding saved diagnostic. Explicit-super forwarding measures 27.546 versus 30.634 ms. Other primitive totals vary between passes. These are loop/call diagnostics rather than official scores or claims of improvement for every native dictionary call.

## Evidence

- [Source/binary identities and validation](data/super-method-call-validation-20261007.json), [fixed gate](data/release-super-method-call-fixed-gate-20261007.json), [gate log](data/release-super-method-call-fixed-gate-20261007.log), [C++ checks](data/super-method-call-cpp-sdk-20261007.log), [Release build](data/build-super-method-call-Release-20261007.log)
- [Official timings](data/pyperformance-xlang3-super-method-call-chameleon-fast-20261007.json), [official log](data/pyperformance-xlang3-super-method-call-chameleon-fast-20261007.log), [run provenance](data/pyperformance-xlang3-super-method-call-chameleon-fast-20261007-provenance.json), [comparison values and hashes](data/super-method-call-chameleon-comparison-20261007.json)
- [CPython fixture reference](data/super-method-call-cpython3147-reference-final-20261007.json), [candidate fixture](data/super-method-call-focused-fixture-20261007.log), [preserved-control fixture](data/super-method-call-preserved-control-fixture-r3-20261007.log), [preserved Release](data/super-method-call-preserved-control-20261007.json), [initial profile-payload assertion](data/super-method-call-preserved-control-fixture-20261007.log)
- [Candidate render identity](data/chameleon-super-method-call-render-identity-20261007.log), [call diagnostic](data/python-super-dict-get-xlang3-super-method-call-20261007.log)
- [Allocation comparison and input hashes](data/super-method-call-allocation-comparison-20261007.json), [control allocation counters](data/super-method-call-preserved-control-allocation-counters-20261007.log), [candidate allocation counters](data/super-method-call-candidate-allocation-counters-20261007.log)
