# Generic trivial Python functions at native callback entry

The ordinary VM call path already avoids a Python frame for eligible functions
that return a constant or argument. Native runtime_call_callable now reuses that
same IR analyzer/executor and shares its observability guard with the ordinary
VM path. This removes repeated Interpreter/frame creation for eligible native
callbacks. Python Counter and all other pure-Python libraries remain Python.
There is no native translation of their library implementation.

The analyzer inspects current code and signature on each entry. It requires
exact positional binding and a capture-free trivial body. Native entry also
excludes generators, async functions and coroutines. Debug stepping, tracing,
profiling, function monitoring and pending asynchronous work retain normal
entry. The asynchronous guard prevents a long native loop from starving queued
work when no surrounding VM safepoint is running. Unsupported signatures,
defaults requiring binding, nontrivial bodies and fallible operations use the
original Interpreter path. Comments explain the cost avoided and guards.

Counter.__missing__ was compiled from the CPython 3.14.7 library and inspected
in a saved IR dump: two positional parameters, no captures/cells, non-generator,
non-async, non-coroutine, and ReturnConst. The fixture verifies identity, ties,
live defaults, invalid arguments, closures, generators/coroutines, overrides,
static/classmethod binding, code replacement, error traceback, caller handled
exception state, trace/profile events and local monitoring.

## Validation and evidence limits

The candidate passed **371 core fixtures**, 11 compatibility sections, three
expected-failure checks, and all eight C++/SDK/graph checks. The unchanged fixed
gate passed all 11 cases with 21 paired repeats, five warmups and a 10% threshold.
The accepted control preserves 140 Release files. Build/run paths remain
`build-repro/main-verify-20261006/Release`; comparison Python is 3.14.7.

Official means: CPython 3.14.7 **3.586 s**, preceding XLang3 **33.784 s**, candidate **32.096 s**. Candidate speed relative to CPython is **0.112×** (**8.95× longer runtime**); nominal speedup over preceding XLang3 is **1.053×**. Candidate sample SD is **0.188 s**. Pyperf instability warning: **False**. These reused references do not establish an alternating-pair official significance claim.

Official BPE uses the original workload, fast mode, common hook/dependency site,
and the unchanged 1,800-second observation cap. Warmups/calibration are excluded
from scoring. This affected case does not replace the separate complete 97-case
comparison or establish a whole-suite win over CPython.

## Paired diagnostics

Each probe uses seven alternating control/candidate process pairs, one warmup
and five samples per row, with checked outputs and unchanged operation counts.
The median of paired control-time/candidate-time ratios is shown below. Higher
than 1× is faster than the preceding XLang3, not CPython. Distinct loop shapes
are diagnostic only. All **770 raw samples** and all 11 summary rows remain.

Native constant-key callbacks and Counter missing reads improve in all seven
pairs. Ordinary nontrivial callback controls are roughly unchanged. The direct
constant-call control is roughly 3% slower in all seven pairs; that observation
is retained rather than excluded from the report. It is separate from the fixed
gate's pass, and small descriptive changes are not significance claims.

| Probe | Mapping | Path | Speedup over preceding XLang3 | Favorable pairs |
| --- | --- | --- | ---: | ---: |
| trivial | — | direct_constant | 0.974× | 0/7 |
| trivial | — | native_max_constant_key | 5.857× | 7/7 |
| trivial | — | counter_missing | 1.566× | 7/7 |
| trivial | — | direct_counter_missing | 0.995× | 3/7 |
| trivial | — | dict_get_default | 0.982× | 3/7 |
| nontrivial | dict | direct_lookup | 0.995× | 2/7 |
| nontrivial | dict | python_call | 0.990× | 3/7 |
| nontrivial | dict | native_callback | 0.995× | 2/7 |
| nontrivial | Counter | direct_lookup | 0.996× | 3/7 |
| nontrivial | Counter | python_call | 1.003× | 4/7 |
| nontrivial | Counter | native_callback | 0.991× | 3/7 |

## Raw evidence

- [Terminal validation](data/native-trivial-callback-validation-20261007.json)
- [Fixed gate](data/release-native-trivial-callback-fixed-gate-20261007.json)
- [Original official log](data/native-trivial-callback-validation-20261007-official-bpe.log)
- [Official comparison / failure record](data/native-trivial-callback-bpe-vs-cpython3147-20261007.json)
- [Original paired results](data/native-trivial-callback-paired-20261007.json)
- [All samples](data/native-trivial-callback-paired-samples-20261007.csv)
- [All summary rows](data/native-trivial-callback-paired-summary-20261007.csv)
- [Initial CPython/native route observations](data/native-trivial-callback-baseline-20261007.json)
- [Counter missing IR](data/native-trivial-callback-counter-missing-20261007.ir.txt)
- [Compiler source provenance](data/native-trivial-callback-source-provenance-20261007.json)
- [Archived scripts and byte-exact compiler inputs](data/native-trivial-callback-20261007-sources/manifest.json)
- [Preserved control provenance](data/native-trivial-callback-preserved-control-20261007.json)
- [Preceding checkpoint](minmax-streaming-checkpoint-20261007.md)
