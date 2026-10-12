# `argparse_subparsers`: current call-dispatch profile (2026-10-03)

## Finding

An instrumented run of the current `/Ob3` scratch build on the repository's
1,000-option `subparsers.py` workload points to the VM call handlers. The
profile attributed **66.8 ms** (24.6% of positive measured self-time) to
`Call` and **57.4 ms** (21.2%) to `CallMethod`. It recorded 36,217 `Call`
dispatches and 51,062 `CallMethod` dispatches. These are diagnostic timer
measurements, not normal-build pyperf scores.

Nested timers put frame setup below the handler cost: `VM_FRAME_RESET` measured
8.9 ms and `VM_CALL_PREPARATION` measured 6.0 ms of exclusive self-time. A
separate probe measured the `CallMethod` pre-lookup checks at 1.44 ms across
65,529 calls and the direct instance-attribute shadow scan at 2.84 ms across
65,529 calls. The remaining cost lies in the cached method dispatch and
call-handler paths after those checks. This makes another argument-buffer,
frame-reset, or attribute-shadow cache an unlikely large win.

## Candidate rejected

I removed an unused `inline_calls_allowed(runtime)` evaluation from the
`CallMethod` handler and documented why the common cached Python/native paths
do not need that global predicate. The 21-pair order-balanced `subparsers`
comparison measured a candidate/control ratio of **1.0067×** (candidate
median 193.292 ms; control median 194.115 ms; 95% interval **0.9870–1.0138**).
The interval crosses parity, so this is not a measured gain. I restored the
original source; no code change from this trial remains.

| Build | Executable SHA-256 | Runtime DLL SHA-256 |
|---|---|---|
| Control | `6E104D63E4905D44B82B1ED60B498772013CA0602DA6575A3D88A0CF979D9875` | `729A27565CA0C99F4A779FC655492A85E2B01A75A46B4C4E31CAC8E6F6158724` |
| Candidate | `6E104D63E4905D44B82B1ED60B498772013CA0602DA6575A3D88A0CF979D9875` | `AB7B68B3C4721D868AAF74FC2B61A0390897D176A140D7F15FD044E72DFC93B4` |

Both builds used the same source snapshot, Python 3.14.7 standard library,
MSVC Release `/O2 /Ob3`, and native-package directory. The fixed Release
executable and DLL were not used or modified. The direct case measurement
checked identical program output on every pair.

## Evidence

- [Unsplit VM timing summary](data/profile-subparsers-timed-20261003.json) and [CSV](data/profile-subparsers-timed-20261003.csv), with [raw workload profile](data/profile-subparsers-timed-20261003.log) and [`-c pass` baseline](data/profile-baseline-timed-20261003.log).
- [Nested call/frame-stage summary](data/profile-subparsers-stages2-20261003.json) and [CSV](data/profile-subparsers-stages2-20261003.csv), with [raw workload profile](data/profile-subparsers-stages2-20261003.log) and [`-c pass` baseline](data/profile-baseline-stages2-20261003.log).
- [CallMethod pre-lookup probe](data/profile-subparsers-callmethod-prelude-20261003.log) and [direct attribute-shadow scan probe](data/profile-subparsers-attr-scan-20261003.log).
- [21-pair candidate/control result](data/callmethod-inline-gate-check-subparsers-20261003.json).

The operation counts and timing ratios for all pyperformance definitions are
in the [same-version full comparison](pyperformance-xlang3-ob3-corrected-full-fast-20261003.md).
The next runtime investigation should split the warmed `CallMethod` cache-hit
path from its fallback path and focus on shared dispatch costs. Pure-Python
standard-library modules remain Python.
