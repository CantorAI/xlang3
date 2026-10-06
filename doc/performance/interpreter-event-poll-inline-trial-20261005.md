# Interpreter event-poll inlining trial (2026-10-05)

## Result

Rejected. Moving the 64-opcode event-poll countdown from
`interpreter_events.cpp` into an inline definition in the VM-visible header
preserved the weakref hint and cross-thread polling policy, but did not make
representative Python workloads faster. The source was restored to the
out-of-line implementation and the restored Release executable passed the
fixture suite and interpreter CTest.

The candidate and saved current-main control were built with the same MSVC
Release configuration. Both used Python 3.14.7's pyperformance 1.14.0
benchmark sources and the same dependency site. Runs used pyperformance
`--fast`; they are directional and do not meet the suite's 1% stability goal.

| Official pyperformance case | Current-main control | Inline candidate | Candidate / control |
|---|---:|---:|---:|
| `comprehensions` | 166 µs ± 1 µs | 167 µs ± 2 µs | 1.01× slower |
| `pickle_pure_python` | 5.13 ms ± 0.08 ms | 5.15 ms ± 0.05 ms | statistically indistinguishable |
| `argparse_subparsers` | 145 ms ± 1 ms | 144 ms ± 1 ms | statistically indistinguishable |
| `async_tree_none` | 4.41 s ± 0.03 s | 4.44 s ± 0.03 s | 1.01× slower |

The small `argparse_subparsers` difference does not establish a win. The
`pyperf compare_to` output hid it as statistically insignificant. Likewise,
it hid the pure-Python pickle comparison. The only displayed changes were
slight slowdowns, so a full fixed-regression gate was not warranted for this
discarded candidate.

The corresponding same-day CPython 3.14.7 reference results remain much
faster: approximately 14 µs for comprehensions, 273.5 µs for pure-Python
pickle, 8.15 ms for subparsers, and 226 ms for `async_tree_none`. The inline
change therefore did not address any of the measured gaps. See the
[full 97-case comparison](pyperformance-xlang3-sparse-instr-cache-vs-cpython314-fast-20261005.md)
for complete definitions, failures, and raw CPython results.

## Reproduction and raw results

The saved control executable was
`build-repro/async-false-guard-control/xlang3.exe` (SHA-256
`84DD7D0A2B90CFA4A1369F507E50FBDB18B85F7A498CA958771648FA4A1971DE`), with
runtime DLL SHA-256
`6CCA3D95F4A6CFEF94B50D68EA0873F2EA379A2094DA342743878C05BB4AFEDA`.
The candidate changed only `interpreter_events.h` and
`interpreter_events.cpp` for the trial; its changes were removed afterward.

- [Control: comprehensions and pickle JSON](data/event-poll-inline-control-fast-20261005.json)
- [Control: comprehensions and pickle log](data/event-poll-inline-control-fast-20261005.log)
- [Candidate: comprehensions and pickle JSON](data/event-poll-inline-candidate-fast-20261005.json)
- [Candidate: comprehensions and pickle log](data/event-poll-inline-candidate-fast-20261005.log)
- [Control: async tree and subparsers JSON](data/event-poll-inline-control-async-fast-20261005.json)
- [Control: async tree and subparsers log](data/event-poll-inline-control-async-fast-20261005.log)
- [Candidate: async tree and subparsers JSON](data/event-poll-inline-candidate-async-fast-20261005.json)
- [Candidate: async tree and subparsers log](data/event-poll-inline-candidate-async-fast-20261005.log)

The full fixture runner passed on the candidate and again after restoring
main. `xlang3_interpreter_tests` passed on both builds. The candidate build
did not run the complete fixed Release gate because its focused official
pyperformance results showed no benefit.
