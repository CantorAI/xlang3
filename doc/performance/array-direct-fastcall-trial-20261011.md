# Rejected array `GetItem` direct-fastcall trial — 2026-10-11

## Result

Rejected. The candidate completed all five official pyperformance SciMark
subtests, but was slower than the saved same-day fixed-Release control in
every case. `pyperf compare_to` reports a 1.41x slower geometric mean:

| Official pyperformance case | Saved control | Direct-fastcall candidate | Candidate / control |
|---|---:|---:|---:|
| `scimark_fft` | 991 ms | 1.25 s | 1.27x slower |
| `scimark_lu` | 1.01 s | 1.54 s | 1.53x slower |
| `scimark_monte_carlo` | 551 ms | 698 ms | 1.27x slower |
| `scimark_sor` | 843 ms | 1.21 s | 1.44x slower |
| `scimark_sparse_mat_mult` | 17.5 ms | 27.4 ms | 1.57x slower |
| **Geometric mean** |  |  | **1.41x slower** |

Both runs used pyperformance 1.14.0, Python 3.14.7, the fixed Release
executable path, and fast mode. Several candidate results were flagged as
unstable by pyperf, so the exact regression size is directional; the uniform
regression across five cases is enough to reject this implementation.

## Change tested and diagnosis

The trial added an integer-only native callback for `array.array` indexing and
an opt-in direct-call branch in the general VM `GetItem` handler. It also
threaded the native-call scratch buffer and execution-lock references through
the `GetItem` opcode dispatch. The `__index__`, slice, and subclass behavior
were routed through or checked against the existing semantic paths. All 55
CTest checks passed with the trial enabled.

The callback itself only applies to arrays, but routing its opt-in through the
general `GetItem` handler requires additional operands and dispatch plumbing
on the shared interpreter path. The broad regressions are consistent with
that overhead outweighing the array-specific saving. The implementation and
its fixture additions have been removed; the durable `GetItem` comments and
unrelated working-tree changes were preserved.

## Reproduction artifacts

- Candidate pyperf data: [`scimark-array-direct-fastcall-candidate-20261011.pyperf.json`](data/scimark-array-direct-fastcall-candidate-20261011.pyperf.json)
- Saved control: [`scimark-current-fixed-release-full-fast-20261011.pyperf.json`](data/scimark-current-fixed-release-full-fast-20261011.pyperf.json)
- Candidate runtime DLL SHA-256: `2219683DFA1487A34F33AC46E1FFB589A88895C84C2513A743EA43B3297DF1B2`
- Candidate executable SHA-256: `D896FCB67AC4848EE802D279F6D9B1BEFDB3F7280C1DF5C58B1C2A8FA288E6E7`
- Fixed runtime path: `build-repro/main-verify-20261006/Release`
- Validation with candidate enabled: 55/55 CTest checks passed.

The fixed Release output was rebuilt from the restored source, and all 55
CTest checks passed. A fresh SciMark run returned to baseline range: pyperf
found no significant change in four of five cases; LU was 1.02x slower, for a
1.01x slower geometric mean. The restored runtime DLL hash is
`D5A0A11C84A8C9090AAFCB87591B245FD6589DE4690D27101C7A2980BC5E6CC6`; the
executable hash is `6633C0A520DABCECA4DA4046F78243569045FFDEFA51160C3883A4CB0C6DF590`.
See [`scimark-restored-source-20261011.pyperf.json`](data/scimark-restored-source-20261011.pyperf.json)
and its [provenance](data/scimark-restored-source-20261011.pyperf.provenance.json).
