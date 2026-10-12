# Rejected positional fast-call adapter for `_lru_cache_wrapper` — 2026-10-11

## Result

Rejected. The official `typing_runtime_protocols` fast run measured 2.75 ms
on the control and 3.02 ms on the candidate; `pyperf compare_to` reported the
candidate at 1.10x slower. Samples were noisy, and the candidate did not
demonstrate a speedup, so its source and fixture changes were removed.

| Official pyperformance case | Control | Candidate | Candidate / control |
|---|---:|---:|---:|
| `typing_runtime_protocols` | 2.75 ms ± 0.07 ms | 3.02 ms ± 0.26 ms | 1.10x slower |

## Hypothesis and change

The native XLang `_functools._lru_cache_wrapper` had no positional fast
callback, so the generic VM call path materialized positional arguments before
calling its native implementation. A diagnostic counter recorded 125,505
slow wrapper calls in one instrumented official benchmark body. The trial
added a fast adapter that copied up to four arguments into retained stack
storage and used a heap fallback for larger calls, then called the existing
cache implementation unchanged. It preserved the existing runtime hashing,
equality, locking, cache eviction, and wrapped-call behavior.

The callback-transfer vector was not the measured bottleneck: its removal did
not improve the full protocol benchmark and the candidate was slower in this
run. The dominant cost remains inside the generic cache work and the Python
protocol/static-lookup operations. Do not retry this adapter without a new
measurement that isolates argument materialization as a material share of
time.

## Validation and artifacts

- Control pyperf data: [`typing-runtime-protocols-lru-fastpath-control-20261011.json`](data/typing-runtime-protocols-lru-fastpath-control-20261011.json)
- Candidate pyperf data: [`typing-runtime-protocols-lru-fastpath-candidate-20261011.json`](data/typing-runtime-protocols-lru-fastpath-candidate-20261011.json)
- Candidate runtime DLL SHA-256: `DA75A4106C847B2AC524A74C2F93F64CBB2D27515AF88B016F9363DF4E380237`
- Control runtime DLL SHA-256: `D5A0A11C84A8C9090AAFCB87591B245FD6589DE4690D27101C7A2980BC5E6CC6`
- Executable SHA-256 for both runs: `6633C0A520DABCECA4DA4046F78243569045FFDEFA51160C3883A4CB0C6DF590`
- The focused lru-cache fixture passed with the candidate. One full CTest run timed out in the Visual Studio debugpy launch check; the isolated check passed immediately afterward. After reverting, the full suite passed 55/55.
- Restored runtime DLL SHA-256: `0A256617FFF23ACF21D0E19A57F423195FA4E7359EE0C10DD1895589BCA63D1C`.
- Fixed Release path: `build-repro/main-verify-20261006/Release`.
- Restored-source pyperformance rerun: 2.91 ms ± 0.21 ms. The control,
  candidate, and restored samples varied enough that no speedup claim is
  justified; see [restored data](data/typing-runtime-protocols-lru-fastpath-restored-20261011.json)
  and [provenance](data/typing-runtime-protocols-lru-fastpath-restored-20261011.provenance.json).
