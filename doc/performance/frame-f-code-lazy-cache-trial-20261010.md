# Observed frame code-object allocation trial

The original coverage workload previously allocated and freed 1,213,976
CodeObjects and 242,847 FrameObjects on XLang3. The attribution receipt is
`data/coverage-f-code-allocation-r3-20261010.json` (SHA256
`fa009691724f782fe619d519b91c62eeda209a03563648496b55c7d0693f010d`).
These are allocation counts, not CPU-time shares or a measured cache speedup.
The benchmark backend differs: CPython uses CTracer and XLang3 uses PyTracer.
An unchanged pure-Python coverage library remains part of this experiment.

Accepted R7b `frame.f_code` reads construct a new CodeObject from the retained
module/function pair. Repeating that getter in an observer therefore repeats
allocation and retirement. The proposed generic runtime change materializes
the code once per observed Python FrameObject and retains it until that frame
is released. Ordinary VM frames do not gain a cache or additional ownership.
The cached Value is included in GC and weak-reference graph edge accounting.
It is not a Runtime-wide owning cache.

## CPython reference and existing behavior

The frozen strict five-group fixture covers retained frames, clear/release,
active function-code replacement, suspended generators, trace callbacks and
tracebacks. The two separate probes require canonical function-code identity
and assigned filename/name/qualname/first-line metadata. Their assertions
remain strict and are not weakened to match XLang3.

| Before applying the cache | Five-group fixture | Canonical identity | Assigned metadata |
|---|---|---|---|
| CPython 3.14.7 | Pass | Pass | Pass |
| Accepted XLang3 R7b | Fail | Fail | Fail |
| Guarded numeric XLang3 trial | Fail | Fail | Fail |

Both XLang3 builds fail the fixture's first repeated `frame.f_code` identity
assertion. The canonical identity and assigned metadata failures predate this
cache proposal. A cache of the frame's existing materialization does not
repair those separate defects; they must remain disclosed and independently
tested. Post-change focused results are recorded below. The combined build's
fresh separate canonical/metadata probes still fail and remain disclosed.

The CP-first unscored receipt is `data/frame-f-code-r2-reference-20261010.json`,
SHA256 `04a8c326a4393ad9ab7ee2dfc3c5d97e45a1c32d643aa378347cca5abacdb029`.
It preserves all nine actual process exits and raw stdout/stderr, the strict
source/expected-output hashes, CPython executable/DLL, accepted control
executable/DLL, and the current trial's recorded source/Release inputs.
All recorded inputs remained unchanged. This is semantic reference evidence,
not a speed measurement or candidate acceptance.

## Candidate status

The R2 proposal preserves the current numeric-method changes.
Its patch SHA256 is
`39e79918d7c249623e61357fff6e562d06dc2edbf29e1164dda7fa4c4e38afd9`;
provenance SHA256 is
`4b7da282acf73b4eb3da569a28268b5c262fa95270c818d4d4f078f3555afbb0`.
Current145 recorded inputs plus the explicit weakref module supplement give
146 parent inputs; adding one fixture and its output gives148. Static reviews
found that the numeric declaration and runner registrations are retained.

The numeric parent is still unaccepted despite a valid 1.065861x original
raytrace benefit. Applying the cache requires preserving that complete
unaccepted build separately, keeping the fixed accepted baseline, then
obtaining fresh combined correctness and performance evidence. The original
coverage and raytrace benchmarks and the complete11-case fixed gate are
required before committing engine changes. Neither source review nor the
allocation count establishes a win over CPython 3.14.7.

The cache was applied after preserving all146 parent source inputs and
178 Release files in a separate archive explicitly marked unaccepted.
The application receipt is
`data/frame-f-code-cache-r2-trial-20261010-application.json`, SHA256
`37f0cc99feec21b72481a257b287e92fb04ceb047f309aaaa6fe801338390f3a`.
The fixed Release path and accepted baseline were retained. The combined
candidate has148 recorded source inputs and13 cumulatively owned files,
including seven engine files. Its fresh Release build passed (receipt
`data/frame-f-code-cache-r2-trial-20261010-build.json`, SHA256
`249f35338294c2e6a8e822ed107db3035e2c50b47b5dd46acdbfed26c607a081`).

Both strict registered fixtures passed on that build: all five frame-cache
groups and the unchanged eight numeric-method groups. This establishes stable
repeated frame-code reads, including clear/release, active replacement,
suspended execution, trace callbacks and tracebacks, without losing the
numeric shortcut's tested semantics. The focused receipt is
`data/frame-f-code-cache-r2-candidate-semantic-20261010.json`, SHA256
`dc9c6fd38888dbecb9ee4ef4be7d0d087a87d2a6414961f25086ced67eabcfb4`.
All recorded source, Release, fixed-baseline and protected inputs remained
unchanged. Fresh correctness, original-workload allocation eligibility and
the fixed gate subsequently completed as recorded below; no previous
correctness or performance acceptance was inherited. Affected original
official benchmark timings subsequently completed as recorded below.

Fresh registered combined correctness subsequently passed:408 core fixtures,
11 compatibility fixtures,3 expected-error cases, all55 actual CTests and
both SQLite API cases. The receipt is
`data/frame-f-code-cache-r2-correctness-20261010.json`, SHA256
`8d33f97d67b41d8d0566a8d2f44385fc34369d839787165940e69bbc4c1236db`.
Source/Release/baseline/protected input hashes and process cleanup checks
passed. This run authenticated the actual post-combined focused executions;
it did not inherit the numeric parent's full correctness. The two fresh
separate strict probes remain failures and are excluded from the registered
suite's success claim. Subsequent performance validation is recorded below.

## Original-workload allocation eligibility

The unchanged original `bench_coverage(1)` allocation diagnostic completed
on CPython3.14.7 and the combined XLang3 candidate. The child uses the same
previously validated read-only `POINTER(c_uint64)` counter recipe; it does
not use unsupported buffer writes or pointer casts. The result is
`data/coverage-frame-f-code-cache-allocation-r2-20261010.json`, SHA256
`ae54450ac47984c2cd86a15e6a8089d4afe17833a3945a22b21a8b5ab1cf46fb`.

| XLang3 object kind | Historical allocations | Combined candidate allocations | Removed |
|---|---:|---:|---:|
| Code | 1,213,976 | 242,798 | 971,178 (about80%) |
| Frame | 242,847 | 242,847 | 0 |

Final releases matched allocations for both kinds in both runs. The original
source, package version, counter layout and reported workload results match;
trace, profile and counter enablement were restored. This verifies the
intended allocation reduction, not CPU dominance or a speed ratio. The older
receipt's transitive/configuration provenance limits remain disclosed.

An external CTest process was active during the new diagnostic. Private
process-local counts remain valid, while **both measurement-validity flags
are false** and the complete activity records are retained. No elapsed time
from this attempt may be scored. R1's malformed absence-path ledger was
held during static review before execution; corrected R2 checks all seven
paths individually and preserves R1 as unexecuted preparation.

The combined candidate's first fixed-gate attempt refused this external
CTest process before launching a benchmark child. Its invalid receipt is
`data/frame-f-code-cache-r2-fixed-gate-20261010.json`, SHA256
`596f82f83208e65235b7eede1242b301d47595539fa2daaa33cd415525355014`.
It provides no performance scores and cannot permit an engine commit.

After that specific external CTest process exited and the process inventory
was idle, a fresh complete fixed-gate run passed all11 cases with21 repeats,
5 warmups and the unchanged0.10 threshold. No worker exception was used.
The valid receipt is
`data/frame-f-code-cache-r2-fixed-gate-r2-20261010.json`, SHA256
`8040c95e3317039e5db4bcc68c90ce0d171e86a777d7da32d75b85dad120551b`.
All input hashes remained unchanged. This establishes the fixed regression
gate for the combined candidate; it does not make the earlier refusal valid
or establish an official coverage/raytrace speed ratio. Affected original
official benchmark measurements remain required before an engine commit.

## Official timing attempts

The first original coverage fast20 attempt timed out on the accepted R7b
control at the unchanged 300-second full-case cap, before starting the
combined candidate. No score or speed ratio was produced. The activity
watcher found no competing tools, cleanup passed, and input hashes remained
unchanged. The raw timeout is retained in
`data/frame-f-code-cache-r2-original-coverage-20261010.json`, SHA256
`ce090d9230a032087ce933d0b5bf731e2ebdcae43a4cd447066a059261d548ed`.
A reviewed cap-only controller now allows900 seconds for the coverage case
and960 seconds for its manager. Raytrace retains300/420 seconds. Reversing
the two changed timeout lines recovers the first controller exactly; original
benchmark work, all20 scored values, guards and cleanup remain unchanged.
It used a fresh output prefix; the completed run is recorded below. Its frozen controller
SHA256 is `3bd39e9fb415ecee2dc9cc9a2b1b2ccf54a76856195795b414e79242a6b06aa4`;
proof SHA256 is
`22f19c900cfc83551c0d52a9caba0f5f18d98ccb15f0fc3b2f5b8124aba712bf`.

The first original raytrace attempt refused an external CTest process before
launching any benchmark child. It provides no score. Its receipt is
`data/frame-f-code-cache-r2-original-raytrace-20261010.json`, SHA256
`f94d82098a88880109a96aae98d6159d30c1110b2bb8c6638b534eb0ea95efa6`.

After that specific CTest process exited, a fresh original raytrace fast20
comparison completed successfully. Its receipt is
`data/frame-f-code-cache-r2-original-raytrace-r2-20261010.json`, SHA256
`7f6e916236e63a66b964623a5cb14770f6ee8c032ae4fa3cfbd1db1eb3eddd54`.
Both phases passed timing activity, cleanup and input-identity checks.

| Original raytrace, 100×100 | Mean seconds | Speed versus accepted XLang3 |
|---|---:|---:|
| Accepted R7b XLang3 | 2.160608235 | 1.000000× |
| Combined numeric-method/cache candidate | 2.049611095 | 1.054155× |

This is 5.14% less elapsed time. Each runtime supplied all20 scored values.
The sequential comparison is unpaired and compares the combined build with
accepted XLang3; it does not isolate the cache's effect or establish a
CPython comparison.

The cap-only original coverage fast20 comparison subsequently passed on both
builds. Its receipt is
`data/frame-f-code-cache-r2-original-coverage-cap900-r2-20261010.json`, SHA256
`e489e3703744c521b449908ad1d89ab397ddd096c020037043515c8a098873cb`.
Both phases supplied20 scored values and passed activity, cleanup and input
checks. No worker exception was used.

| Original coverage | Mean seconds | Speed versus accepted XLang3 |
|---|---:|---:|
| Accepted R7b XLang3 | 12.307487315 | 1.000000× |
| Combined numeric-method/cache candidate | 12.123253940 | 1.015197× |

The observed elapsed-time reduction is1.50%, a small result from a sequential,
unpaired comparison. It is not a cache-only causal estimate. Removing about
80% of code-object allocations did not produce a large timing improvement;
allocation counts must not be presented as a fivefold speedup. The native
CTracer versus Python PyTracer backend difference remains a separate design
issue to investigate, without assigning it an unmeasured CPU-time share.

The combined candidate has now passed fresh registered correctness, the
complete unchanged fixed gate and both affected original official benchmarks.
It is eligible for a checkpoint. The full97/all124 CPython3.14.7 comparison
has not been refreshed, and the goal of a substantial whole-suite improvement
remains open. The two separate strict canonical-code/assigned-metadata
failures remain disclosed above.
