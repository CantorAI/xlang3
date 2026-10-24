# Genshi timed rendering investigation — 2026-10-07

The native XML correctness checkpoint is on main at `383b7eeb`. The valid official
Genshi result remains 18.64× slower for text and 24.51× slower for XML than
**CPython 3.14.7**. This investigation identifies runtime targets; it changes no
engine or Genshi implementation and establishes no new official speedup.

## Bounded uninstrumented phases

Three sequential paired runs use the same installed Genshi and official
`bm_genshi` template/input. Table values are medians, not pyperf results.
Generation excludes serialization and retains all 1,000 rows and 10,000 cells.
Each expression phase checks a 10,000-result checksum. The timings are overlapping
alternative probes, not disjoint phases; do not add them together or infer exact
percentages of the official benchmark from them.

| Work | CPython 3.14.7 | XLang3 | XLang3 time / CPython time |
|---|---:|---:|---:|
| Generate text events (32,002) | 18.860 ms | 313.912 ms | 16.64× |
| Generate XML events (34,004) | 24.149 ms | 379.993 ms | 15.74× |
| Context.get, 10,000 calls | 1.272 ms | 27.317 ms | 21.47× |
| lookup_name, 10,000 calls | 1.675 ms | 33.866 ms | 20.22× |
| eval of compiled code, 10,000 calls | 3.773 ms | 97.551 ms | 25.86× |
| Expression.evaluate, 10,000 calls | 6.829 ms | 135.132 ms | 19.79× |

Already compiled expression evaluation is much slower. Code inspection finds
that each explicit-dict eval copies globals into a module and invokes
`Interpreter::run_module`, including module metadata/slot preparation, before
executing the code. CPython comparisons must retain live globals/locals,
overrides, reentry, builtins and exception behavior: caching stale namespace
contents or translating Genshi lookup code into C++ would be invalid.
The [CPython 3.14.7 implementation](https://github.com/python/cpython/blob/v3.14.7/Python/bltinmodule.c#L905)
retains the supplied globals/locals and calls `PyEval_EvalCode` for code objects.
It does not copy the namespace into a synthetic module.

A bounded live-namespace probe exposes five current XLang3 mismatches:

| Observation | CPython 3.14.7 | XLang3 |
|---|---:|---:|
| Read global after a callback updates it | 2 | 1 |
| Insert missing builtins into original globals | true | false |
| `globals()` preserves supplied dictionary identity | true | false |
| Nested function observes a later global change | 3 | 2 |
| Supplied custom `len` builtin | 42 | 2 |

Explicit locals shadowing globals passes on both. These are baseline failures,
not newly introduced changes. They make a snapshot cache an invalid solution.
Next: implement live generic eval globals with appropriate builtins, reentry and
code-frame semantics, then measure the official Genshi case and run the complete
correctness suite/fixed gate. Generator entry and native
string/regex serialization remain additional targets.

## Profiling limits and workload checks

Python call counts match for key operations: both runtimes call
`Expression.evaluate` 11,001 times and `lookup_name` 12,002 times per render.
Generator profile call/resume counts also match (for example `_flatten`:
32,003 text and 34,005 XML). XLang3's native `_lsprof` creates separate entries
for repeated frame-code objects: 314,505 text entries and 613,954 XML entries
versus 243/253 CPython entries. Grouped counts are useful; instrumented elapsed
and self times are heavily distorted and must not be used as speed scores.
The reusable probe now groups by source location and records entry counts to
bound its report output. Original raw evidence is retained losslessly in gzip
with hashes in the summary.

Native sampling starts at a fresh marker after template/input construction.
Twenty unprofiled render passes produced 1,350 text and 3,540 XML samples.
Both final output hashes and counts match CPython exactly. Samples include
interpreter/frame handling, value copying, attribute lookup and system-library
work. Release symbolization lacks source locations and can associate private
addresses with nearby public symbols: raw addresses are retained, and no exact
function-cost percentages are claimed. Sampling perturbs execution and is
never a timing result.

The executable stays at
`D:\CantorAI\xlang3\build-repro\main-verify-20261006\Release\xlang3.exe`.
DLL SHA-256 is `b182715318bccf05236d82390c413020de73c8b97bfac673caa87f081a914acb`.
No build or competing measurement ran during the sequential probes. The
historical full 97-case dataset and fixed regression baseline remain unchanged.

## Evidence

- [Phase samples and checksums](data/genshi-runtime-phases-20261007.json)
- [Live eval namespace baseline and five differences](data/eval-live-namespace-baseline-20261007.json)
- [Grouped reporter validation on both runtimes](data/genshi-grouped-profiler-validation-20261007.json)
- [Grouped profiles and raw compressed-file hashes](data/genshi-render-body-profile-summary-20261007.json)
- [Original profile record, gzip](data/genshi-render-body-profile-20261007.json.gz)
- [Text native samples](data/genshi-render-text-native-body-20261007.json)
- [Text exact-output provenance](data/genshi-render-text-native-body-20261007-provenance.json)
- [XML native samples](data/genshi-render-xml-native-body-20261007.json)
- [XML exact-output provenance](data/genshi-render-xml-native-body-20261007-provenance.json)
- [Valid official benchmark checkpoint](pyexpat-namespace-incremental-checkpoint-20261007.md)
