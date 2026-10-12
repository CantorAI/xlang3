# Two-argument numeric method execution trial

This trial targets generic VM method-call overhead observed in the unchanged
official `raytrace` benchmark. It leaves the Python library and benchmark
sources unchanged. It recognizes existing IR; it does not change compiler
lowering or emit a new IR representation.

The first implementation has a useful measured speed gain, but is **held**:
an additional finalizer probe found that omitting the method frame changes an
observable retirement context. Its passing regression gate does not override
that result. A revised guard is being validated separately. Neither version
has established a win over CPython 3.14.7 or refreshed the full suite results.

## Revised candidate: validation in progress

The revised candidate adds four conservative checks before execution: cached
inherited finalizer presence, weak-reference observer roles, native/owned
object payloads, and an old Object at the caller's destination. Receiver
attributes must be immediate values or native String/Bytes leaves without
weak-reference roles. Other cases use ordinary method execution. The existing
finalizer predicate is shared through a declaration; no new object fields,
cache implementation, or native library replacement is added.

This candidate rebuilt successfully at the existing path. It passed the
unchanged eight-group semantic fixture, a fresh complete407/11/3 Python
fixture run, and all55 CTests including both SQLite API cases. Its original
temporary, alias, and replacement lifetime transcripts match the accepted
control (raw object addresses are retained but ignored when comparing the
replacement transcript). This is control-relative preservation, **not** a
claim that the existing strict CPython failures now pass.

The guarded original-worker counter result still removes 5,178,166
dispatches with the same arithmetic-family pattern. Seven alternating
control/candidate diagnostic pairs measured a median speed ratio of
1.058373x, with a bootstrap 95% interval of [1.053652, 1.071044]. All pairs
are retained, including one 0.870150x slow result. This is a diagnostic
comparison against accepted XLang3, not CPython or an official suite result.
The first candidate's 1.074633x official result must not be attributed to
the revised candidate. A separate valid original official fast20 run of
the guarded candidate measured 2.152485 seconds for accepted XLang3 and
2.019480 seconds for the candidate: 1.065861x speed. Both runtime phases
passed activity, cleanup, and source/Release integrity checks. These fresh
sequential means are not a paired confidence interval or a CPython result.

The guarded candidate's full R7 fixed gate did not pass. Ten cases passed;
GC traversal remained inconclusive in both attempts (elapsed-time ratios
1.090347 and 1.080491, respective 95% intervals [1.050447, 1.139844] and
[1.036099, 1.127591]). Independently, one process scan refused a known
worker identity/CPU or parent/child mismatch. The failed sample recorded
only a generic error, so later matching worker snapshots cannot establish
its cause or certify that attempt. The complete gate remains required
before an engine checkpoint; this official benefit does not override it.
The later combined numeric-method and frame-code-cache build has now passed
fresh408/11/3 fixtures, all55 CTests/API checks and the complete unchanged
11-case fixed gate. This does not recertify the numeric-only failed gate.
See [the combined trial](frame-f-code-lazy-cache-trial-20261010.md) for its
separate receipts and completed affected official timings: the combined build
measured1.054155× raytrace and1.015197× coverage versus accepted R7b XLang3,
with20 scored values per runtime and no worker exception. These sequential
unpaired results are not CPython comparisons or a numeric-only causal
estimate. The combined build is eligible for a checkpoint; the numeric-only
failed gate remains invalid.

The revised official R4 and R5 attempts produced no score: their control
preflight refused external build/test activity. R4 observed a newly created
MSBuild worker; R5 observed an active foreign CTest process. These receipts
remain invalid and must not be reclassified as successful measurements.
Separate two-snapshot proofs authenticate only an unchanged orphan reusable
MSBuild worker and its console children. They do not exempt active builds
or tests, certify machine idleness, or reset CPU baselines between phases.

Revised receipts in `data` are
`two-argument-double-ir-plan-r3-trial-20261010-application.json`,
`two-argument-double-ir-plan-r3-trial-20261010-build.json`,
`two-argument-double-ir-plan-r3-candidate-semantic-20261010.json`,
`two-argument-double-ir-plan-r6-correctness-20261010.json`,
`two-argument-ir-guarded-lifetime-r2-20261010.json`, and
`two-argument-ir-raytrace-counters-r3-20261010.json`,
`two-argument-ir-raytrace-paired-r4-20261010.json`,
`two-argument-ir-raytrace-official-r4-20261010.json`, and
`two-argument-ir-raytrace-official-r5-20261010.json`, plus the successful
`two-argument-ir-raytrace-official-r6-20261010.json` and invalid full gate
`two-argument-double-ir-plan-r7-fixed-gate-20261010.json`. A separate lifetime
harness preflight refusal launched no child: a basename-only pin filter
mistook the XLang3 `python.exe` launcher for the CPython reference executable.
The corrected controller pins the exact CPython paths and preserves that
failed preflight receipt.

## Mechanism and constraints

The bounded executor recognizes a synchronous two-argument method whose
leading zero-argument method call returns its receiver, followed by exact
Double field arithmetic. It removes an ordinary Python activation and VM
dispatches only after the entire shape and operand checks pass. Names are not
specialized to a library, class, field, or benchmark. Rejection takes the
original Python call path once.

Checks cover current function/code ownership, signatures, register bounds and
fresh writes (including the auxiliary receiver register), dynamic guard
overrides, descriptors, custom attribute hooks, exposed dictionaries,
recursion headroom, and trace/profile/debug/monitoring observers. Arithmetic
keeps the original instruction order; it does not reassociate operations or
introduce fused multiply-add. Source comments explain these requirements.

The lifetime investigation adds another admission requirement: a missing
method frame must not change observable finalization. The existing
class-version finalizer cache can make that check inexpensive; weak-reference
roles are available in the existing object header. These checks are generic
runtime behavior, not native replacements of pure-Python library code.

## First implementation: measured benefit, acceptance held

Both official runs used the original benchmark source, the same dependency
site and shim, and `--mode fast` (20 scored values each). Process guards,
source and Release hashes, actual exits, and cleanup are recorded in the raw
receipts. The comparison is previous accepted XLang3 versus experimental
XLang3, not CPython versus XLang3.

| Official raytrace | Mean seconds | Speed relative to accepted XLang3 |
| --- | ---: | ---: |
| Accepted XLang3 control | 2.148957350 | 1.000000x |
| Initial numeric-method trial | 1.999712950 | 1.074633x |

The separate seven alternating fresh-worker pairs gave a median speed ratio
of 1.076998x, with paired bootstrap 95% interval [1.071660x, 1.080829x]. This is
a prefilter; it is not itself the official benchmark result. Aggregate
original-worker counters removed 5,178,166 dispatches, consistent with
369,869 occurrences of the observed 14-instruction arithmetic family.
Counters include entry/setup and do not establish exact shortcut-hit counts
or CPU time shares.

The initial candidate passed the strict eight-group semantic fixture, fresh
all55 CTest/API checks, and the unchanged complete eleven-case performance
gate (21 repeats, five warmups, 10% threshold). An earlier complete gate was
inconclusive on GC traversal and remains preserved. The full Python fixture
result was authenticated from its completed raw result after verifying the
sole lingering console helper had retired; its original failed watcher flags
remain unchanged. All these results describe the initial candidate only.

## Additional lifetime evidence

The unregistered CP-first probe is kept with its strict expected output and
raw results. CPython 3.14.7 passes. The accepted XLang3 and initial trial fail
the temporary-input group; their finalizers observe different frame names.
This is an actual trial behavior difference and blocks acceptance.

Independent selections prevent that first failure from hiding later checks:

| Check | CPython 3.14.7 | Accepted XLang3 | Initial trial |
| --- | --- | --- | --- |
| Temporary input finalizer context and caller locals | Pass | Fail | Fail; differs from control |
| Aliased receiver/argument retires once | Pass | Pass | Pass |
| Displaced output observes published replacement | Pass | Fail | Fail; same pre-existing behavior |
| Receiver-owned payload finalizer context | Pass | Fail | Fail |

The payload assertions can themselves trigger exception cleanup, so an error
message's subsequently populated event list is not proof that those events
were present when the assertion ran. No sorting defect or precise payload
retirement timing is inferred from that transcript. The original assertion
and a separate equivalent length/membership formulation are both preserved.
The control's unexpected exception rendering is also retained as observed
data, without claiming its root cause has been diagnosed.

## Authoritative raw receipts

All paths below are relative to `doc/performance/data`:

- Application/build: `two-argument-double-ir-plan-r2-trial-20261010-application.json`
  and `two-argument-double-ir-plan-r2-trial-20261010-build.json`.
- Strict candidate semantics: `two-argument-double-ir-plan-r2-candidate-semantic-20261010.json`.
- Correctness: `two-argument-double-ir-plan-r4-correctness-20261010.json`, with
  the earlier R1/R2 failed harness receipts preserved alongside it.
- Dispatch evidence: `two-argument-ir-raytrace-counters-r2-20261010.json`.
- Alternating pairs: `two-argument-ir-raytrace-paired-r2-20261010.json`.
- Official benchmark: `two-argument-ir-raytrace-official-r2-20261010.json`.
- Complete passing gate: `two-argument-double-ir-plan-r5-fixed-gate-20261010.json`;
  prior inconclusive gate: `two-argument-double-ir-plan-r4-fixed-gate-20261010.json`.
- Lifetime: `two-argument-ir-lifetime-20261010.json`,
  `two-argument-ir-lifetime-alias-20261010.json`, and
  `two-argument-ir-lifetime-replacement-20261010.json`.
- Owned payload: `two-argument-ir-owned-payload-20261010.json` and
  `two-argument-ir-owned-payload-r2-20261010.json`.

The preserved accepted control is
`build-repro/controls/gc-generic-cycles-r7b-accepted-20261009`. The fixed
cumulative-regression baseline remains `build-repro/Release/xlang3.exe`.
Candidate execution remains at
`build-repro/main-verify-20261006/Release/xlang3.exe`. Archives do not change
the run path. Six pre-existing unrelated dirty tracked files are protected
and excluded from this trial's owned edits.

The previously published full97/all124 report remains a historical result
for the accepted control. A one-case trial cannot update its overall speed,
failure counts, or CPython comparison.
