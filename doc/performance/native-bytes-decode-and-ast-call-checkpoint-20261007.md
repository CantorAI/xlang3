# Native decode exposure and AST call preservation — 2026-10-07

Chameleon now passes native `bytes.decode` class lookup and retains the arguments needed by its Python AST templates. The candidate passes complete correctness and the unchanged Release performance gate. Its official Chameleon run still fails, so this checkpoint has no Chameleon timing or CPython speed ratio. The overall performance goal remains unfinished.

## Native runtime changes

`bytes.decode` and `bytearray.decode` are exposed on their canonical native classes, forwarding to the existing codec implementation and keyword parser. Canonical owner state is retained through Runtime native-package cleanup, so saved descriptors continue to work when Python builtin names are rebound. Receiver validation checks native storage or actual subclass membership, rather than a class name or a forged private payload. Python subclass overrides retain ordinary dispatch. Instance codec fast paths are unchanged.

The AST expression shortcut previously recognized a call but always constructed empty `args` and `keywords`. Consequently `ast.parse('getname(KEY)', mode='eval')` lost `KEY` before any Python visitor ran. The shortcut now accepts only empty calls with a representable callee; argument-bearing calls and unsupported callees fall through to the existing complete parser/converter. It no longer fabricates `Constant(None)` for an unsupported callee. Code comments explain the guarded inexpensive path and why argument preservation is necessary.

Both changes concern native runtime facilities. Python `ast.py`, its visitors/unparser, and Chameleon remain Python; no pure-Python library algorithm was translated into C++.

## Diagnosis and corrected inference

The bytes-only official attempt failed with `get_name() missing ... 'key'`. A direct and saved `Scope.get_name('options')` call both worked, disproving the initial method-binding hypothesis. Generated source contained `getname()['table']`. A minimal `NodeTransformer` appeared to lose its argument, but inspecting the freshly parsed tree showed it was already absent. The fault was parsing, not Python visitor replacement or list slicing.

After the guard, both the ordinary Python visitor and Chameleon's Python template visitor produce `getname('options')` with one argument. The saved method probe still binds correctly, and an empty-table render succeeds (54 characters). That diagnostic is not the full benchmark.

The official benchmark now reaches populated table rendering and fails at `__quote() missing 1 required positional argument: 'default_marker'`. Its root cause is not established here. The run exits 1 without a timing JSON; there is no score to compare or splice into the frozen full-suite report. Earlier failed attempt logs are retained.

## Validation

- CPython **3.14.7**, `C:\Python\Python314\python.exe`, provides both new fixture references.
- All **358 core fixtures, 11 section fixtures and 3 expected-failure checks** pass on the candidate. All **eight C++/SDK/graph checks** pass.
- All **11 default gate cases** pass, using **21 paired repeats, 5 warmups and the unchanged 10% threshold**, exit 0. Accepted baseline remains `build-repro/Release/xlang3.exe`.
- Candidate remains at `build-repro/main-verify-20261006/Release/xlang3.exe`. No candidate run path, build configuration, threshold or baseline was changed.
- Official Chameleon uses the existing shared dependency sources, compatibility hook, fast mode and 300-second definition cap. Executable, runtime DLL and hashlib hashes match at start/end. No benchmark completed.

The AST fixture compares complete exec/eval expression trees, exercises compiled positional/keyword/unpacked/nested calls and returned callees, and verifies Python AST rewriting and argument locations. The decode fixture checks class/instance calls, genuine subclasses, keyword parsing, invalid receivers, saved owner identities and exception propagation. These fixtures do not claim complete AST or codec ABI coverage.

## Performance context

The preceding pushed weakref checkpoint measured GC traversal at nominal **3.1243×** the saved CPython 3.14.7 speed. Runtime protocols improved **5.2%** from the preceding XLang3 result but remained **24.0× slower** than CPython. Those are earlier targeted measurements, not new scores for this candidate. See the [weakref checkpoint](weakref-registry-index-checkpoint-20261007.md).

The [frozen complete 97-definition run](pyperformance-xlang3-subscription-dispatch-full-fast-20261007.md) remains unchanged: 66 completed definitions and 31 failed definitions. New targeted attempts must not be presented as a rerun of all 97. Historical CPython binary provenance limits remain documented in the [saved-reference audit](data/cpython3147-saved-reference-provenance-audit-20261007.json).

## Evidence

- [Compiled source identities and validation](data/native-ast-call-arguments-validation-20261007.json), [fixed gate](data/release-native-ast-call-fixed-gate-20261007.json), [gate log](data/release-native-ast-call-fixed-gate-20261007.log), [C++ checks](data/native-ast-call-arguments-cpp-sdk-20261007.log), [build log](data/build-native-ast-call-arguments-Release-20261007.log)
- [AST CPython fixture reference](data/native-ast-call-arguments-cpython3147-reference-20261007.json), [candidate fixture](data/native-ast-call-arguments-focused-20261007.log), [decode CPython reference](data/native-bytes-decode-cpython3147-reference-20261007.json)
- [Latest official failure log](data/pyperformance-xlang3-native-ast-call-chameleon-fast-20261007.log), [run identities](data/pyperformance-xlang3-native-ast-call-chameleon-fast-20261007-provenance.json), [repaired Python template probe](data/native-ast-call-template-transform-probe-20261007.log), [scope/render probe](data/chameleon-native-ast-call-scope-binding-probe-20261007.log), [generated source](data/chameleon-native-ast-call-generated-source-20261007.txt)
- [Preserved preceding Release](data/native-bytes-decode-preserved-control-20261007.json), [bytes-only candidate checks](data/native-bytes-decode-validation-20261007.json), [bytes-only official failure](data/pyperformance-xlang3-native-bytes-decode-chameleon-fast-20261007.log), [bytes-only run identities](data/pyperformance-xlang3-native-bytes-decode-chameleon-fast-20261007-provenance.json)
- [Original generated source](data/chameleon-native-bytes-decode-generated-source-20261007.txt), [original method probe](data/chameleon-native-bytes-decode-scope-binding-probe-20261007.log), [original minimal AST probe](data/native-bytes-decode-ast-transform-xlang3-probe-20261007.log), [CPython probe](data/native-bytes-decode-ast-transform-cpython3147-probe-20261007.log)
