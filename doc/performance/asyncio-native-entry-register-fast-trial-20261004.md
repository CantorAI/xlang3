# Asyncio native entry register-backed calls (2026-10-04)

## Result

The official pyperformance 1.14.0 `async_tree_eager` fast run measured the
current-source control at **2.97 s ± 0.05 s** and the candidate at
**2.91 s ± 0.04 s**, or **1.02× faster**. Pyperf warned that the run did not
have enough samples for a stable result, so this is a small directional gain,
not evidence of a large speedup. CPython 3.14.7 measured **86.62 ms ± 1.69 ms**
in the matching full run; the candidate remains about **33.6× slower**.

The change supplies the existing register-backed native-call adapters to
`_asyncio._get_running_loop`, `_asyncio.get_running_loop`, and
`_asyncio.current_task`. Before the change, a one-loop instrumented workload
counted 9,339 slow `_asyncio._get_running_loop` calls and 9,331 slow
`_asyncio.current_task` calls. `_asyncio.get_running_loop` uses the same adapter
but was not separately present in those counters. Afterward the two hot names
no longer appear among slow native calls. They reach the same callbacks
through the positional fast-call convention. Keyword handling remains on the
existing callback path.

The profile also counted 9,332 slow `_asyncio.Future.__init__` calls. Adding a
method fast-call adapter did not change that count: class construction reaches
the generic runtime constructor callback instead of the VM register-backed
native-call path. That ineffective adapter was removed. This identifies native
class construction as a separate shared-runtime optimization target.

A generic runtime bridge was also tried and rolled back before benchmarking:
the existing `len` adapter expects all arguments in register slots, while the
custom `str.join` fast callback expects its receiver in the leading slot and
the iterable in a register. The runtime path needs explicit fast-call layout
metadata before it can safely reuse every native adapter.

## Measurements and validation

| Runtime | `async_tree_eager` mean |
| --- | ---: |
| CPython 3.14.7 | 86.62 ms ± 1.69 ms |
| XLang3 current-source control | 2.97 s ± 0.05 s |
| XLang3 native-entry fast-call candidate | 2.91 s ± 0.04 s |

- Candidate and control used pyperformance 1.14.0, the Python 3.14.7
  dependency site, and the repository's Windows compatibility shim.
- The full Python fixture suite and `xlang3_interpreter_tests.exe` passed on
  the final rebuilt candidate.
- The fixed Release baseline gate passed all 11 cases, with the largest
  candidate/baseline ratio at **1.081×** for `subparsers`.
- The preserved baseline executable hash remains
  `B70A6A046513883F808F088C43BC64B7BF7C9672728E74205F3B67AAAADA52DA`.
- Candidate Release executable hash:
  `1AC96E9D3907F4CCF938146C8C3A40E5DF1EFE65489AC47556F884119FB57C75`.
- Candidate runtime DLL hash:
  `F5F7A23AF6CF85A1490EA6F50FF7ABCFA30C3788E206F0DA6C75383785C8DF4D`.

Raw data: [control](data/async-tree-native-entry-fast-control-20261004.json),
[candidate](data/async-tree-native-entry-fast-verified-20261004.json),
[one-loop call counters](data/async-tree-native-entry-fast-counters-20261004.txt),
and [fixed-baseline regression gate](data/release-regression-async-native-entry-fast-final-20261004.json).

The implementation comment is beside the `_asyncio` registrations in
`src/runtime/modules/system/asyncio_module.cpp`. This improves one hot path by
a few percent while leaving the main asyncio gap intact; it is not a broad
performance reversal.
