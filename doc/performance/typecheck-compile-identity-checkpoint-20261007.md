# Generic type-check guard and compile identity checkpoint

Negative `isinstance` checks on immediate values now avoid instance-attribute probing after full class-info and metaclass-hook validation. Compilation accepts genuine string/bytes subclasses and resolves AST nodes through private canonical class identities, preserving Python hooks/descriptors and exception types. The native translation of `typing.Protocol`'s pure-Python member loop is removed; the original Python hook now executes.

All 353 core fixtures and 11 section fixtures pass, including the three focused correctness fixtures. All eight compile identity probes and eight C++/SDK checks pass. The unchanged complete Release gate passes all 11 cases, with 21 order-balanced paired repeats, five warmups and a 10% threshold. See the [source/hash/validation record](data/typecheck-compile-validation-20261007.json), [focused outputs](data/typecheck-compile-targeted-xlang3-r2-20261007.log), [C++/SDK log](data/typecheck-compile-cpp-sdk-20261007.log), and [fixed gate](data/release-typecheck-compile-fixed-gate-20261007.json). The full fixture runner exits silently on success; its exit status and exact runner/source hashes are recorded.

## Paired dispatch evidence

The [15-case diagnostic](data/typecheck-compile-row-dispatch-paired-20261007.json) uses seven repeats in both executable orders, 28 worker processes, retained sample values, and matching start/end binary hashes. The control is the preserved exact `23d1d443` executable. Construction and binding occur outside timed bodies. These ratios compare XLang3 builds; they are not CPython speed factors or official pyperformance scores.

| Diagnostic | Old XLang3 / new XLang3 speed |
|---|---:|
| `primitive_typecheck_false` | 1.124x |
| `primitive_typecheck_true` | 1.022x |
| `python_checked_row_getitem` | 1.060x |
| `python_checked_row_saved_getitem` | 1.079x |
| `list_row_getitem` | 0.978x |

The other subscription/saved-method paths are roughly unchanged, with nominal variation of a few percent. The checked row path remains Python; no SciMark algorithm is translated into C++.

## Official targeted benchmark results

All three selected definitions were attempted with the same 300-second complete-definition cap. SciMark and typing complete; Chameleon fails. Exit code 1 records that failure, and executable/runtime/native hashlib hashes match at start/end in the [provenance](data/pyperformance-xlang3-typecheck-compile-targeted-fast-20261007-provenance.json). The [raw pyperf JSON](data/pyperformance-xlang3-typecheck-compile-targeted-fast-20261007.json), [full log](data/pyperformance-xlang3-typecheck-compile-targeted-fast-20261007.log), and [comparison values/input hashes](data/typecheck-compile-official-comparison-20261007.json) retain the evidence. Calibration/warmups are excluded. Fast-mode stability warnings remain; the following are nominal arithmetic means, without a significance claim.

| Subtest | Saved CPython 3.14.7 seconds | Previous XLang3 seconds | Candidate seconds | Old / candidate | Candidate speed, CPython = 1x |
|---|---:|---:|---:|---:|---:|
| `scimark_fft` | 0.250560575 | 0.99653186 | 0.981728325 | 1.0151x | 0.2552x |
| `scimark_lu` | 0.07664069 | 1.5101651 | 1.4030097 | 1.0764x | 0.0546x |
| `scimark_monte_carlo` | 0.06026091 | 0.571300055 | 0.56957682 | 1.0030x | 0.1058x |
| `scimark_sor` | 0.105248185 | 0.86539767 | 0.876087285 | 0.9878x | 0.1201x |
| `scimark_sparse_mat_mult` | 0.0032493718 | 0.0182405469 | 0.0178093363 | 1.0242x | 0.1825x |
| `typing_runtime_protocols` (prohibited native shortcut) | 0.000132394478 | 0.000252724785 | 0.00326972531 | 0.0773x | 0.0405x |

LU is nominally 1.076x faster than the preceding full-run checkpoint, consistent with the roughly 6–8% gain in the checked-row diagnostic. It remains about 18.3x slower than CPython. The other SciMark differences are small; do not claim five significant improvements.

The old typing result executes the prohibited native member loop; it is not an acceptable baseline for compliant Python execution. Returning to the real Python hook costs about 12.9x against that implementation and leaves the candidate about 24.7x slower than CPython. The profiling fixture verifies execution of the original typing hook. Keep the algorithm in Python and investigate generic attribute/type-check/call costs instead of restoring the native library translation.

## Remaining failure and comparison scope

Chameleon progresses past the previous Token(str) compilation failure, then fails in Python's AST unparser because an Assign node has no optional `type_comment` attribute. No Chameleon score exists. The [native AST constructor source audit](native-ast-constructor-defaults-source-audit-20261007.md) compares CPython's field defaults/schema and sets out the next generic repair. This checkpoint does not establish full compile/AST compatibility.

This three-definition result does not replace the [frozen all-97 report](pyperformance-xlang3-subscription-dispatch-full-fast-20261007.md), which measures the preceding binary. Its saved CPython reference has the [documented provenance limitations](data/cpython3147-saved-reference-provenance-audit-20261007.json). There is no full-suite CPython win or restored August performance claim. The performance goal remains unfinished.
