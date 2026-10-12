# Reference-count release ordering trial (2026-10-03)

## Change and result

On non-final `Value` releases, the runtime previously used an acquire-release
atomic decrement. The candidate uses a release decrement and pays an acquire
fence only when the count reaches zero, before finalization. This follows the
standard reference-counting pattern: ordinary VM register/frame drops still
publish their writes, while the final owner synchronizes before destroying the
object. The inline comment beside `release()` records why the memory ordering
is deliberately asymmetric.

The change is retained. A 21-pair order-balanced same-source Release gate
passed all 11 cases. The best-supported focused gains were **1.018× faster** on
`function_calls` (1.260 ms to 1.208 ms; paired 95% interval 0.9729–0.9976×)
and **1.016× faster** on `gc_traversal` (8.126 ms to 7.629 ms; interval
0.9485–0.9963×). The Telco-shaped Decimal loop was effectively unchanged
(0.9998× candidate/control, interval 0.9928–1.0108×); pickle writer and
deepcopy were inconclusive. This is a small general runtime gain, not a large
solution to the remaining pyperformance gap.

Official pyperformance 1.14.0 on CPython 3.14.7 measured
`pickle_pure_python` at **5.68 ± 0.14 ms** on control and **5.62 ± 0.22 ms** on
candidate. `gc_traversal` measured **1.37 ± 0.12 ms** on both builds.
`pyperf compare_to` found neither official pair significant, so no official
pyperformance speedup is claimed. The control and candidate A/B executables
were separate snapshots; the fixed path
`D:\CantorAI\xlang3\build-repro\Release\xlang3.exe` was not moved or renamed.

## Correctness and artifacts

The C++ interpreter and runtime-value tests passed. Focused Python fixtures for
weakrefs, weakref thread lifetime, signals, pickle, and native Decimal passed.
The full fixture runner stopped at `ctypes_pointer_return` because libffi is
unavailable in this build; this is an environment limitation, so no full
fixture pass is claimed.

The 11-case Release gate and raw paired workload data are preserved in
[`fixed-11-case-gate.json`](../../scratch/performance-trials/refcount-release-ordering-20261003/fixed-11-case-gate.json),
[`function-calls-ab.json`](../../scratch/performance-trials/refcount-release-ordering-20261003/function-calls-ab.json),
[`gc-traversal-ab.json`](../../scratch/performance-trials/refcount-release-ordering-20261003/gc-traversal-ab.json),
and [`telco-decimal-ab.json`](../../scratch/performance-trials/refcount-release-ordering-20261003/telco-decimal-ab.json).
The official JSON runs are [`pickle control`](../../scratch/performance-trials/refcount-release-ordering-20261003/official-pickle-control.json),
[`pickle candidate`](../../scratch/performance-trials/refcount-release-ordering-20261003/official-pickle-candidate.json),
[`GC control`](../../scratch/performance-trials/refcount-release-ordering-20261003/official-gc-control.json),
and [`GC candidate`](../../scratch/performance-trials/refcount-release-ordering-20261003/official-gc-candidate.json).

## Full Python 3.14 pyperformance rerun

After retaining this change, I reran all 97 pyperformance 1.14.0 definitions
on the fixed Release executable, using `C:\Python\Python314\Lib` and the
saved CPython 3.14.7 reference. The run completed 46 definitions, recorded 51
failures/timeouts, and matched 50 subtests. XLang3 won 3; the geometric mean
CPython/XLang3 ratio was **0.14794×**, versus **0.14658×** (4 wins) immediately
before this trial. Fast-mode variance is too high to attribute that small
difference to the refcount change, but the full suite does not show a broad
gain. The largest gaps remain `telco` (0.021×), `async_tree_eager` (0.026×),
`pickle_pure_python` (0.048×), and `subparsers` (0.051×).

The refreshed all-97 chart, status list, matched subtests, and raw run are
[`the report`](pyperformance-xlang3-python314-refcount-release-full-fast-20261003.md),
[`SVG chart`](pyperformance-xlang3-python314-refcount-release-full-fast-20261003.svg),
[`status CSV`](data/pyperformance-xlang3-python314-refcount-release-full-fast-20261003-all-97-status.csv),
[`subtest CSV`](data/pyperformance-xlang3-python314-refcount-release-full-fast-20261003-subtests.csv),
[`raw JSON`](data/pyperformance-xlang3-python314-refcount-release-full-fast-20261003.json),
and [`runner log`](data/pyperformance-xlang3-python314-refcount-release-full-fast-20261003.log).
The executable hash remained `C10F14F7D876E371F747803770109E76888AF7B704EA8E56973738320D051492`;
the runtime DLL hash was `A7278E14500C52312966CB2E92956119F9638D4D6CB42475DAA990C268AAE61A`.
As in earlier runs, optional-package import errors and slow cases account for
many failures; they are listed per benchmark in the status CSV.

The measured control SHA-256 values were `fcda3d2e86d2f6022d7a968fed7d2293d2f43574e78386d65b65bec45aa03b2e`
(`xlang3.exe`) and `9df239dd07e84a795424a812b749a3418610e43e318970082e8e66127f57c22a`
(`xlang3_runtime.dll`). Candidate values were `c10f14f7d876e371f747803770109e76888af7b704ea8e56973738320d051492`
and `a7278e14500c52312966cb2e92956119f9638d4d6cb42475daa990c268aae61a`.

The full CPython 3.14 comparison still has much larger gaps, including Telco,
eager asyncio, pure-Python pickle, and argparse. Continue optimizing the shared
VM and runtime hot paths; this refcount change does not close the overall goal.
