# Builtin literal namespace and completed Chameleon benchmark — 2026-10-07

Chameleon now completes the official benchmark with identical rendered output in XLang3 and CPython 3.14.7. It remains nominally **15.03× slower** than the saved CPython reference. This checkpoint closes a benchmark failure and establishes a usable workload for optimization; it does not establish a speedup over a previously failing run or completion of the overall performance goal.

## Native runtime repair

XLang3 omitted `None`, `False` and `True` from its native builtin namespace. Chameleon's unchanged Python code enumerates `builtins.__dict__` to construct a reverse symbol map. Without `None`, its Python AST transformer returns a bare `None` replacement, which deletes the corresponding call argument. The generated `__quote` call consequently had four arguments instead of five.

The runtime now registers the canonical literal values and exports them through the native `builtins` module. This follows [CPython 3.14.7's native builtin initialization](https://github.com/python/cpython/blob/v3.14.7/Python/bltinmodule.c#L3275). Literal evaluation retains the existing immediate-value path; no namespace lookup was added to each instruction. Comments document that distinction and the need for namespace introspection.

The rebinding fixture then exposed a related AST shortcut defect: `ast.parse('None', mode='eval')` produced a `Name`, so compiling that tree could read a rebound builtin entry. The native shortcut now produces `Constant` nodes for all three keyword literals, independently of rebinding. Failed initial fixture logs are retained. Python `ast.py`, Chameleon and their AST visitors remain Python.

## Official measurement

| Benchmark | Saved CPython 3.14.7 | XLang3 candidate | CPython time / XLang3 time |
|---|---:|---:|---:|
| chameleon | 11.851 ms | 178.068 ms | 0.06655× |

Ratios above 1× favor XLang3. Each result has 20 timed values. These are nominal fast-mode means; the stability warning remains, and the saved CPython reference has [historical binary provenance limits](data/cpython3147-saved-reference-provenance-audit-20261007.json). The selected official run exits 0, with executable/runtime DLL/hashlib identities unchanged between start and finish. It is not a fresh full 97-definition run and must not be spliced into the frozen report.

Separate warmed profiling passes render **222,553 characters**, with matching UTF-8 SHA-256 `ee20adc6250db78d5443e8d50cc9e940f448151dab8ce51e5d83aea93531616c`. Both show 26,506 Python `Scope.get` calls, 16,002 `get_name` calls and 10,000 generated `__quote` calls. XLang3 also exposes one Python functools-partial event absent from CPython. Profiling disables optimizations and substantially changes execution time; its inclusive/self times are not benchmark scores.

## Shared call-cost investigation

A five-sample diagnostic with 1,000 warmup calls measures 20,000 operations per sample:

| Operation | CPython median total | XLang3 median total |
|---|---:|---:|
| Saved native dict.get | 0.560 ms | 7.401 ms |
| Saved native super.get | 0.648 ms | 8.772 ms |
| Python wrapper calling dict.get | 1.400 ms | 24.530 ms |
| Python wrapper calling super().get | 2.632 ms | 44.325 ms |
| Python wrapper calling explicit super(Scope, scope).get | 2.546 ms | 30.393 ms |

These totals include Python loops and function-call overhead, and are not official pyperf scores. They identify shared call and implicit-super costs as candidates for investigation. Source inspection finds that the zero-argument super constructor materializes `runtime.current_locals_snapshot()` before reading one receiver; the fallback defining-class inference can also scan the MRO/class attributes. This is a measured hypothesis for the next generic runtime change, not a demonstrated isolated bottleneck or a completed optimization. Do not translate the Python Scope algorithm into C++.

## Correctness and unchanged gate

All **359 core fixtures, 11 section fixtures and three expected-failure checks** pass; all **eight C++/SDK/graph checks** pass. The new fixture verifies canonical builtin entries, namespace introspection, AST literal shape, Python symbol replacement and literal independence from builtin rebinding.

All **11 default fixed-gate cases** pass with **21 paired repeats, five warmups, the unchanged 10% threshold and exit 0**. Accepted baseline remains `build-repro/Release/xlang3.exe`; candidate remains `build-repro/main-verify-20261006/Release/xlang3.exe`. Builds finish before measurements, and benchmarks run sequentially. The preceding validated Release is preserved separately without replacing the accepted baseline.

## Evidence

- [Source/binary identities and validation](data/builtin-constants-validation-20261007.json), [fixed gate](data/release-builtin-constants-fixed-gate-20261007.json), [gate log](data/release-builtin-constants-fixed-gate-20261007.log), [C++ checks](data/builtin-constants-cpp-sdk-final-20261007.log), [final build](data/build-builtin-literal-namespace-ast-Release-20261007.log)
- [Official timings](data/pyperformance-xlang3-builtin-constants-chameleon-fast-20261007.json), [official log](data/pyperformance-xlang3-builtin-constants-chameleon-fast-20261007.log), [run provenance](data/pyperformance-xlang3-builtin-constants-chameleon-fast-20261007-provenance.json), [comparison values and hashes](data/builtin-constants-chameleon-comparison-20261007.json)
- [CPython fixture reference](data/builtin-constants-cpython3147-reference-20261007.json), [candidate fixture](data/builtin-constants-focused-fixture-20261007.log), [initial fixture failure](data/builtin-constants-focused-fixture-initial-failure-20261007.log), [initial full-suite failure](data/builtin-constants-full-fixtures-initial-failure-20261007.log), [preserved Release](data/builtin-constants-preserved-control-20261007.json)
- [CPython template probe](data/builtin-constants-chameleon-cpython3147-probe-20261007.log), [preceding XLang3 probe](data/builtin-constants-chameleon-preserved-control-probe-20261007.log), [candidate probe](data/builtin-constants-chameleon-candidate-probe-20261007.log). The probe's direct ast.unparse output is incomplete for Chameleon's custom AST nodes on both runtimes; its argument count is the diagnostic. Full rendering uses Chameleon's Python generator.
- [XLang3 warmed call profile](data/chameleon-builtin-constants-xlang3-callprofile-20261007.log), [CPython warmed call profile](data/chameleon-builtin-constants-cpython3147-callprofile-20261007.log), [render equivalence](data/chameleon-builtin-constants-render-equivalence-20261007.json)
- [CPython super/dict diagnostic](data/python-super-dict-get-cpython3147-20261007.log), [XLang3 super/dict diagnostic](data/python-super-dict-get-xlang3-builtin-constants-20261007.log)
