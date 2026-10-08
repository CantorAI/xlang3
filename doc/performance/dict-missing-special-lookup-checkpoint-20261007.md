# Dictionary missing-key special lookup

XLang3 now resolves dict-subclass __missing__ on the type, matching CPython
3.14.7 dict_subscript. Instance attributes cannot shadow this special method.
Ordinary raw Python/native methods receive owned self/key arguments directly,
avoiding a BoundMethod object and an owning argument vector on every miss.
Static/class methods, properties and user descriptors retain class-based binding;
binding failures keep their Python exception and traceback. Current class lookup
observes method replacement and deletion. Counter remains the original Python
library, and tracing/monitoring remain on the generic Python call path.

The same dispatcher serves both runtime miss branches and explicit native
dict.__getitem__. Native membership and setdefault query storage without invoking
subclass __getitem__ or __missing__, as do get and pop. Performance comments
explain the allocation avoided and the protocol/ownership constraints.

## Correctness and validation

The preserved pre-change differential shows instance shadowing, broken custom
descriptor binding, swallowed descriptor exceptions, and setdefault incorrectly
calling __getitem__. All **13** corrected protocol observations match CPython
3.14.7. A new six-part fixture additionally verifies live class mutation, static/
class/property binding, error tracebacks, storage semantics and tracing.

The candidate passed **372 core fixtures**, 11 compatibility sections, three
expected-failure checks, eight C++/SDK/graph checks, and the unchanged fixed
Release gate: 11 cases, 21 paired repeats, five warmups and 10% tolerance.
The accepted control preserves 140 Release files. The build/run directory remains
build-repro/main-verify-20261006/Release. No pure-Python library was translated
into C++ and no CPython native extension was reused.

Official means: CPython 3.14.7 **3.586 s**, preceding XLang3 **32.096 s**, candidate **31.699 s**. Candidate speed relative to CPython is **0.113×** (**8.84× longer runtime**); nominal speedup over preceding XLang3 is **1.013×**. Candidate sample SD is **0.139 s**. Pyperf instability warning: **False**. These reused references do not establish an alternating-pair significance claim.

Official BPE retains the original workload, fast mode, shared hook/dependency
site and 1,800-second cap. Warmups/calibration are excluded from scoring. This
case does not replace the separate complete 97-case comparison or establish a
whole-suite win against CPython.

## Paired diagnostics

Seven alternating process pairs, five samples and one warmup per row retain
**770 raw samples** and all 11 rows. Ratios above 1× mean faster than the preceding
XLang3. Missing-key reads improve in all seven pairs. Some unchanged controls
measure slower, including dict.get/default and direct lookup; those rows are
retained. Diagnostic ratios are separate from the fixed gate and official score.

| Probe | Mapping | Path | Speedup over preceding XLang3 | Favorable pairs |
| --- | --- | --- | ---: | ---: |
| trivial | — | direct_constant | 1.065× | 6/7 |
| trivial | — | native_max_constant_key | 1.022× | 4/7 |
| trivial | — | counter_missing | 1.348× | 7/7 |
| trivial | — | direct_counter_missing | 1.018× | 4/7 |
| trivial | — | dict_get_default | 0.950× | 1/7 |
| nontrivial | dict | direct_lookup | 0.953× | 2/7 |
| nontrivial | dict | python_call | 0.987× | 2/7 |
| nontrivial | dict | native_callback | 0.997× | 3/7 |
| nontrivial | Counter | direct_lookup | 0.966× | 0/7 |
| nontrivial | Counter | python_call | 0.985× | 2/7 |
| nontrivial | Counter | native_callback | 0.996× | 2/7 |

## Evidence

- [Terminal validation](data/dict-missing-special-lookup-validation-20261007.json)
- [Fixed gate](data/release-dict-missing-special-lookup-fixed-gate-20261007.json)
- [Official log](data/dict-missing-special-lookup-validation-20261007-official-bpe.log)
- [Official comparison](data/dict-missing-special-lookup-bpe-vs-cpython3147-20261007.json)
- [Pre-change differential](data/dict-missing-special-lookup-before-20261007.json)
- [Corrected differential](data/dict-missing-special-lookup-after-20261007.json)
- [Original paired results](data/dict-missing-special-lookup-paired-20261007.json)
- [All samples](data/dict-missing-special-lookup-paired-samples-20261007.csv)
- [All summary rows](data/dict-missing-special-lookup-paired-summary-20261007.csv)
- [Compiler source provenance](data/dict-missing-special-lookup-source-provenance-20261007.json)
- [Archived scripts and compiler inputs](data/dict-missing-special-lookup-20261007-sources/manifest.json)
- [Preserved control](data/dict-missing-special-lookup-preserved-control-20261007.json)
- [Preceding checkpoint](native-trivial-callback-checkpoint-20261007.md)

Reference implementation: [CPython 3.14.7 Objects/dictobject.c](https://github.com/python/cpython/blob/v3.14.7/Objects/dictobject.c), dict_subscript.
