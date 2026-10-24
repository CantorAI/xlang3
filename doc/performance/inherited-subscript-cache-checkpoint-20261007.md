# Inherited subscript cache checkpoint

This change reduces generic inherited subscript dispatch overhead. Python
`collections.Counter` and the BPE algorithm remain Python. The comparison
reference is CPython 3.14.7; diagnostic speedups below compare the new XLang3
candidate with the preceding XLang3 checkpoint, not with CPython.

## Implementation and lifetime rules

GetItem and SetItem already cached direct Python/native methods. Their miss
path now resolves inherited methods through the existing class lookup API.
Receiver class identity and the class version guard each weak cache entry.
Base mutation recursively invalidates derived class versions. Native eligibility
still requires ordinary descriptor binding, no expression capture, and a valid
callback; custom descriptors and static/class methods keep normal binding.
Python methods retain their VM frame and error traceback. Cache entries do not
create persistent owners of functions or classes.

Class attribute replacement now retains an aliased incoming value, detaches
the replaced owner, publishes the new value, and invalidates lookup caches
before releasing the old owner. Deletion similarly invalidates before release.
This prevents finalizer reentry from seeing stale methods or stale weak cache
pointers. The before probe observed the old descriptor during replacement in
XLang3 but the new descriptor in CPython. The new fixture verifies the corrected
ordering, inherited native/Python methods, mutation/deletion, base reassignment,
binding fallbacks, missing keys and traceback frames. Code comments explain the
allocation avoided and the required guards and lifetime ordering.

## Validation

The candidate passed 368 core fixtures, 11 compatibility sections, the three
expected-failure checks, and all eight selected C++/SDK/graph tests. The fixed
Release regression gate passed all 11 cases using its unchanged defaults:
21 paired repeats, five warmups and a 10% threshold. Candidate binary and source
hashes, commands, logs, baseline checks and outcomes are in the validation JSON.
Build and run paths remain `build-repro/main-verify-20261006/Release`.

The official affected BPE run completed; see its raw JSON. The paired diagnostic table below is a separate measurement. The official run used the unchanged workload and fast
mode, with the same compatibility hook and dependency site. Its observation
cap was 1,800 seconds, increased from the preceding trial's retained 1,200-second
timeout because the unchanged full body had taken approximately 44 seconds
per invocation. This cap does not reduce workload or sample count.


The completed official means are CPython 3.14.7 **3.586 s** and XLang3 **34.112 s**. XLang3 speed relative to CPython is **0.105×**, or **9.51× longer runtime**. The CPython reference is reused from the same-day full fast run; this is not an alternating before/after comparison or a significance claim.

Pyperf warns that the candidate result may be unstable and has insufficient
samples for its stated precision criterion. The candidate's sample standard
deviation is approximately **3.4 seconds**. Keep that variation in mind when
interpreting the descriptive mean; no statistically significant improvement
over the preceding XLang3 is claimed from this official run. The preceding
official run timed out and therefore supplies no complete comparison score.

## Paired dispatch diagnostics

Seven alternating control/candidate process pairs each ran one warmup and five
samples per path and key count. Every resulting key value was checked. Each
ratio divides the control median by its paired candidate median; the table
reports the median of those seven ratios. Higher than 1× is faster than the
preceding XLang3. Small changes in direct paths are descriptive, not statistical
significance claims. All 980 raw samples and all 14 summary rows are preserved.

| Dispatch path | Keys | Speedup over preceding XLang3 | Favorable pairs |
| --- | ---: | ---: | ---: |
| exact_dict | 256 | 1.036× | 4/7 |
| direct_native_dict | 256 | 1.022× | 4/7 |
| inherited_native_dict | 256 | 1.214× | 7/7 |
| inherited_native_counter | 256 | 1.225× | 7/7 |
| direct_native_counter | 256 | 0.999× | 3/7 |
| direct_python | 256 | 0.998× | 3/7 |
| inherited_python | 256 | 1.658× | 7/7 |
| exact_dict | 1024 | 1.021× | 5/7 |
| direct_native_dict | 1024 | 1.007× | 5/7 |
| inherited_native_dict | 1024 | 1.233× | 7/7 |
| inherited_native_counter | 1024 | 1.228× | 7/7 |
| direct_native_counter | 1024 | 0.987× | 0/7 |
| direct_python | 1024 | 0.985× | 1/7 |
| inherited_python | 1024 | 1.627× | 7/7 |

These diagnostics do not establish a win over CPython or replace the full
97-case pyperformance comparison. The last full comparison and the preceding
dictionary checkpoint remain separately recorded.

## Evidence

- [Validation and provenance](data/inherited-subscript-cache-validation-20261007.json)
- [Unchanged fixed gate](data/release-inherited-subscript-cache-fixed-gate-20261007.json)
- [Original paired results](data/inherited-subscript-cache-paired-20261007.json)
- [All paired samples](data/inherited-subscript-cache-paired-samples-20261007.csv)
- [Paired summary](data/inherited-subscript-cache-paired-summary-20261007.csv)
- [Original official BPE log](data/inherited-subscript-cache-validation-20261007-official-bpe.log)
- [Official BPE comparison / failure record](data/inherited-subscript-cache-bpe-vs-cpython3147-20261007.json)
- [Archived scripts and hashes](data/inherited-subscript-cache-20261007-sources/manifest.json)
- [Before publication semantics](data/inherited-subscript-cache-class-method-publication-before-20261007.json)
- [Preserved control provenance](data/inherited-subscript-cache-preserved-release-provenance.json)
- [Preceding dictionary checkpoint](dict-intrinsic-index-checkpoint-20261007.md)
- [Last full CPython 3.14.7 comparison](pyperformance-xlang3-live-eval-vs-cpython3147-full-fast-20261007.md)

The source archive also contains byte-exact compiler inputs in `compiled-sources`.
The validation record distinguishes their hashes from Git LF-normalized source
hashes; the source text is identical after newline normalization.
