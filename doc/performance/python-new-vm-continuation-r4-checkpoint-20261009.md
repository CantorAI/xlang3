# Python `__new__` continuation checkpoint

Ordinary class calls with an eligible synchronous Python `__new__` now push that
function onto the current VM stack. After it returns and its observers and frame
cleanup finish, the continuation checks the returned object's current type and
MRO, resolves `__init__` afresh, and pushes an eligible Python initializer. Owned
context tuples retain the selected callables, class and inputs through cleanup,
error unwind and destination replacement. Caller inspection state is restored
before completion owners can reenter Python. Existing native and runtime-call
entry paths are unchanged. Admission is limited to ordinary Call and exact-tuple,
no-keyword CallEx, with explicit observer/debug/pending-event fallback guards.

The final R4 build corrects two unsupported `TupleItems.data()` spellings to the
existing `begin()` API. The failed R3 compile is retained. Both durable fixtures
passed their unchanged CPython 3.14.7 transcripts (eight constructor groups and
one completion-finalizer group), then the rebuilt candidate passed all 19 focused
cases. Fresh complete correctness passed 403 core cases, 11 compatibility sections,
three expected failures, all nine selected CTests and two SQLite API checks. The
same-candidate untimed rows were authenticated and reused for the later timing
continuation. Their raw watcher flags remain false where untimed MSBuild/owned
CTest observations occurred; they are semantic passes, never timing acceptance.

The unchanged default 11-case gate passed with 21 repeats, five warmups and a
0.10 threshold against the preserved fixed baseline. Original
`unpickle_pure_python` completed 20 values on each runtime, using protocol 5,
the original three payloads and 20 loads per payload. The candidate remains
14.38 times slower than CPython 3.14.7 in this fresh sequential, unpaired fast
comparison. Warnings and all values are retained; no CPython win is claimed.

A separate artificial constructor diagnostic used six repetitions per runtime,
all six CP-Python / previous-X / candidate-X permutations, mirrored case orders
and 10,000 operations per case. The date and minimal Python class-call medians
improved relative to the previous X build. The saved `__new__` cases are retained
as controls, including their slower medians. These are artificial entry costs,
not an official workload score, workload fraction or projected suite gain.

The untimed eligibility evidence retains the original `pickle.py` and
`_pydatetime.py` IR, exact tuple/no-keyword CallEx inputs, canonical Python date
`__new__` at line 982, and the repeated ignored Call destination used by the
finalizer case. An initial probe failed and its corrected probe passed without
repeating the already captured IR. This proves the observable guard inputs and
IR; it does not count actual continuation hits or dynamic pending-event state.

Both full97 launches stopped before any benchmark child was started because
foreign build/test processes were present. Their refused records are preserved;
full97 is **pending**, with zero cases started for this candidate. The previously
published 73/97 completed matrix describes an older source/build and stays
separate. This checkpoint's full validation covers the complete correctness
suite, fixed gate and affected original unpickle case.


| Artificial case (µs/call, lower is faster) | CP explicit Python | Previous X | Candidate X | Previous X / candidate |
|---|---:|---:|---:|---:|
| date_class | 0.417090 | 7.867685 | 6.017225 | 1.307527× |
| date_saved_new | 0.367405 | 5.227200 | 5.450920 | 0.958957× |
| plain_class | 0.115855 | 2.019955 | 1.457535 | 1.385871× |
| plain_saved_new | 0.077290 | 0.836715 | 0.856535 | 0.976860× |

The previous-X/candidate ratios above belong only to the balanced artificial diagnostic.

| Original official case | CPython 3.14.7 mean (ms) | Candidate X mean (ms) | X / CP elapsed time |
|---|---:|---:|---:|
| unpickle_pure_python | 0.166483 | 2.393610 | 14.377537× |

[All 72 constructor values](data/python-new-vm-continuation-r4-checkpoint-20261009-constructor-values.csv), [constructor medians](data/python-new-vm-continuation-r4-checkpoint-20261009-constructor-medians.csv), [all 40 official values](data/python-new-vm-continuation-r4-checkpoint-20261009-official-values.csv), [fixed 11 gate summary](data/python-new-vm-continuation-r4-checkpoint-20261009-fixed-gate-summary.csv), [all gate arrays](data/python-new-vm-continuation-r4-checkpoint-20261009-fixed-gate-values.csv).


The measured candidate is the exact recorded source132 worktree and rebuilt
Release178 receipt. Ten owned engine/fixture/runner files form this checkpoint;
pre-existing unrelated dirty files are preserved and excluded. The line endings of six existing owned files follow their HEAD counterparts;
four fixtures/outputs are new.
The source inventory and prior published source128 archives identify the parent
bytes; this is partial compiled-source provenance, not a clean-main reproduction
claim. Runtime/control binaries and native object files stay outside Git. Their
hash maps and the accepted R5 control manifest are retained. Dependency records
cover the named sources, runner/hook, package metadata and selected stdlib files,
with the original transitive-file coverage limits preserved.

[Validated receipt](data/python-new-vm-continuation-r4-full-validation-20261009.json),
[balanced constructor receipt](data/date-constructor-entry-cp-r5-r4-20261009.json),
[original eligibility failure](data/python-new-vm-continuation-r4-eligibility-20261009.json),
[corrected eligibility](data/python-new-vm-continuation-r4-eligibility-r2-20261009.json),
[failed R3 build](data/python-new-vm-continuation-r3-build-20261009.json),
[refused full97 first](data/pyperformance-xlang3-python-new-r4-full-fast-20261009-provenance.json)
and [refused full97 second](data/pyperformance-xlang3-python-new-r4-full-fast-r2-20261009-provenance.json).
