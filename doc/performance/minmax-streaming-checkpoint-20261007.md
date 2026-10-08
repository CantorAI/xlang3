# Native min/max streaming and global lifetime checkpoint

Native min/max now advances one item, calls its key, compares and releases
discarded owners before fetching another item. The previous implementation
collected all items and all keys into two owning vectors before comparing.
That delayed callbacks and finalizers, hid list growth during key callbacks,
and fetched later items after an earlier callback should have raised.

The new path retains O(1) temporary owners and preserves first-item ties,
original result identity, defaults, positional/iterable forms, original
iteration/key/comparison/truth exceptions, and native owner release order.
Comments explain the allocation avoided and observable sequencing. Python
Counter and the BPE algorithm remain Python; min/max is a native CPython builtin.

## VM lifetime repair

The new finalizer test exposed an existing global-object lifetime defect in
both the preserved accepted Release and the first streaming trial. Local
objects finalized promptly, but module-level objects remained retained after
attribute access and deletion, including objects constructed without min/max.
The original before observations and failed intermediate observation remain.

Global/module deletion now publishes the missing binding and updated version
before releasing owners. At this cold deletion boundary it clears obsolete
expression/container registers, completed native-call scratch arguments and
matching owning global-cache values. Future and loop-carried registers remain
protected. Real Python aliases retain their ordinary namespace/container owners.
This adds no scan to ordinary VM instructions.

The fused LoadModuleAttr receiver register is written from the live module slot
before the instruction reads it. Its internal read was incorrectly classified
as requiring an old loop-carried value. Metadata now excludes that internal
read from loop-carried classification while retaining its linear last-use;
later external reads still establish real liveness. The minimal IR dump and
regression fixtures cover immediate loop finalization, aliases, dead native
container temporaries, absent bindings during reentrant finalizers, and min/max
release order. No threshold, workload or correctness assertion was weakened.

## Validation and measurements

The candidate passed **370 core fixtures**, 11 compatibility sections, the
three expected-failure checks, and all eight C++/SDK/graph checks. The unchanged
fixed gate passed all 11 cases at 21 paired repeats, five warmups and a 10%
threshold. The accepted control preserves 140 Release files. Build/run paths
remain `build-repro/main-verify-20261006/Release`; CPython is 3.14.7.

Official means: CPython 3.14.7 **3.586 s**, preceding XLang3 **34.112 s**, candidate **33.784 s**. Candidate speed relative to CPython is **0.106Ã—** (**9.42Ã— longer runtime**); the descriptive speedup over preceding XLang3 is **1.010Ã—**. Candidate sample SD is **0.168 s**. The candidate log has no instability warning. The preceding run did warn and had a much larger sample SD (about 3.350 s). The approximately 1% nominal difference establishes no material or statistically significant speed gain; this is a correctness and memory-footprint checkpoint.

The official run uses the original BPE workload, fast settings, common
dependency site/hook and the same 1,800-second observation cap as the preceding
completed run. Calibration and warmup values are excluded from scoring.
References are reused from completed same-day runs, so nominal changes do not
establish a paired official engine gain. The last full 97-case comparison is
separate; this checkpoint does not establish a whole-suite win over CPython.

Seven alternating control/candidate diagnostic process pairs each used a
warmup and five samples for six paths. All checksums and lookup counts were
verified. Higher than 1Ã— is faster than the preceding XLang3, not CPython.
These rows do not demonstrate a callback speed win: the Counter native callback
was roughly 3% slower, and every measured pair favored the control for that row.
All **420 raw samples** are retained.

| Mapping | Diagnostic path | Speedup over preceding XLang3 | Favorable pairs |
| --- | --- | ---: | ---: |
| dict | direct_lookup | 1.003Ã— | 4/7 |
| dict | python_call | 0.979Ã— | 2/7 |
| dict | native_callback | 0.990Ã— | 3/7 |
| Counter | direct_lookup | 1.016Ã— | 5/7 |
| Counter | python_call | 0.995Ã— | 3/7 |
| Counter | native_callback | 0.966Ã— | 0/7 |

## Evidence

- [Terminal validation](data/minmax-streaming-validation-20261007.json)
- [Fixed gate](data/release-minmax-streaming-fixed-gate-20261007.json)
- [Original official BPE log](data/minmax-streaming-validation-20261007-official-bpe.log)
- [Official comparison / failure record](data/minmax-streaming-bpe-vs-cpython3147-20261007.json)
- [Original paired diagnostics](data/minmax-streaming-paired-callbacks-20261007.json)
- [All paired samples](data/minmax-streaming-paired-samples-20261007.csv)
- [Paired summary](data/minmax-streaming-paired-summary-20261007.csv)
- [Original streaming defect](data/minmax-streaming-fixture-before-20261007.json)
- [Independent before lifetime probe](data/minmax-result-lifetime-observations-20261007.json)
- [Intermediate incomplete lifetime repair](data/minmax-global-delete-r1-lifetime-observations-20261007.json)
- [Minimal loop IR before liveness repair](data/minmax-streaming-global-loop-before-20261007.ir.txt)
- [Compiler source provenance](data/minmax-streaming-source-provenance-20261007.json)
- [Archived scripts and exact compiler inputs](data/minmax-streaming-20261007-sources/manifest.json)
- [Preserved control provenance](data/minmax-streaming-preserved-control-20261007.json)
- [Preceding checkpoint](inherited-subscript-cache-checkpoint-20261007.md)
