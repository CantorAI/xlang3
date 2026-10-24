# Native AST defaults and module ownership checkpoint — 2026-10-07

The candidate restores native `_ast` constructor defaults and metadata needed by Python 3.14.7 libraries. Its full unchanged Release gate passes. Official `gc_traversal` completes and has a nominal 3.00× speed ratio against the saved CPython 3.14.7 reference. Chameleon still fails, now later in generated-source compilation; it has no timing score. The full-suite performance goal remains unfinished.

## Implementation and performance design

The generated schema covers 126 native `_ast` classes, including TemplateStr and Interpolation. It supplies `_field_types`, shared annotation dictionaries, optional class defaults, independent list defaults, and the Load singleton. Native constructors preserve Python attribute hooks and subclass field metadata, reject duplicate positional/keyword fields, and retain 3.14 required/unknown-field deprecation warnings. Python AST visitors, unparser, and Chameleon algorithms remain Python.

Schema expressions are parsed once at module initialization. Constructors use mutable runtime metadata so subclass overrides remain observable. List defaults are fresh per constructor; the Load context is shared. Intrinsic attribute helpers avoid user-rebound builtin lookup and synthetic profiling events while preserving Python hooks. Comments in the implementation explain these constraints.

The first candidate passed correctness but failed the performance gate: GC traversal confirmation was 1.216× baseline time. Public `__module__ = 'ast'` did not identify the classes' actual native `_ast` owner. The generic collector consequently treated the newly enlarged canonical class metadata graphs as local graphs. The repaired candidate records the actual owning module in `globals_module`. Existing collector checks still verify that the owner holds the class; module rebinding remains observable. Collection is not disabled or reduced. Runtime teardown clears module slots before native package state is released.

This checkpoint covers class schema and constructor behavior, not complete AST ABI compatibility or template-string parsing/compiler semantics.

## Validation

- CPython 3.14.7 and XLang3 constructor fixture agree; all 126 native class schemas match the exported CPython reference.
- Complete fixtures pass: 354 core, 11 section, and 3 expected-failure cases. Eight C++/SDK/graph checks pass.
- The fixed accepted baseline is unchanged. All 11 cases pass with 21 paired repeats, 5 warmups and the original 10% tolerance; exit code 0. GC traversal candidate/baseline time is 0.740× (95% paired interval 0.705–0.781). This is a local baseline comparison, not a CPython score.
- Official pyperformance attempts both selected definitions under the unchanged 300-second full-definition cap. GC traversal completes with 20 timed values; Chameleon fails. Overall exit code 1. Executable, runtime DLL and native hashlib hashes match at start/end. Fast-mode stability warnings remain.

## Official timed result

| Case | Saved CPython 3.14.7 | Previous frozen XLang3 full run | Current candidate | CPython / current |
|---|---:|---:|---:|---:|
| gc_traversal | 2.332 ms | 1.351 ms | 0.778 ms | 3.00× |

Ratios above 1× favor XLang3. The nominal previous/current improvement is 1.74×, measured across checkpoints with other runtime changes; it does not isolate this ownership repair. The saved CPython reference has the documented historical binary-hash/worker metadata limitations. This targeted run does not replace or splice into the frozen 97-definition report.

## Remaining Chameleon failure

The prior failure at missing `Assign.type_comment` is resolved. Chameleon's Python compiler now reaches generated-source compilation. Its token-map formatting produces an unquoted string inside a tuple, for example `(python: options['table'], 3, 20)`, which is invalid Python. `Token` inherits from `str`; the Python compiler uses `%r` to render the tuple. The next investigation is generic subclass/container repr dispatch and formatting, with a CPython 3.14.7 fixture before changing the runtime. Chameleon remains a failed worker, not a benchmark gain.

## Evidence

- [Compiled source identities and correctness](data/native-ast-defaults-owner-validation-20261007.json)
- [Passing fixed gate](data/release-native-ast-defaults-owner-fixed-gate-20261007.json) and [gate log](data/release-native-ast-defaults-owner-fixed-gate-20261007.log)
- [Official raw timings](data/pyperformance-xlang3-native-ast-defaults-owner-targeted-fast-20261007.json), [full official log](data/pyperformance-xlang3-native-ast-defaults-owner-targeted-fast-20261007.log), [run provenance](data/pyperformance-xlang3-native-ast-defaults-owner-targeted-fast-20261007-provenance.json), [comparison values and hashes](data/native-ast-defaults-owner-official-comparison-20261007.json)
- [Rejected prototype validation](data/native-ast-defaults-validation-20261007.json), [rejected gate](data/release-native-ast-defaults-fixed-gate-20261007.json)
- [Native CPython schema reference](data/native-ast-cpython3147-schema-reference-20261007.json), [constructor reference](data/native-ast-constructor-defaults-cpython3147-reference-20261007.json), [preserved preceding build](data/native-ast-defaults-preserved-control-20261007.json)
- [CPython reference provenance limits](data/cpython3147-saved-reference-provenance-audit-20261007.json), [frozen full-97 report](pyperformance-xlang3-subscription-dispatch-full-fast-20261007.md)
- [CPython native constructor source audit](native-ast-constructor-defaults-source-audit-20261007.md)
