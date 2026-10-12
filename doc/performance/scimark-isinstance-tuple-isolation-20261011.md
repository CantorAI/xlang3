# SciMark `isinstance` shortcut isolation — 2026-10-11

## Question

Did the guarded `isinstance(x, tuple)` call-site shortcut in
`src/executor/xlang_vm/ops/xlang_vm_ops_call.h` cause the 3.35x SciMark LU
improvement observed between the r3 and r4 Release binaries?

## Method and result

Built a source-matched control by disabling only the shortcut's cache setup
guard, then ran the `scimark` group in pyperformance fast mode against the
fixed Release directory. The candidate used the same source with that guard
enabled. Both runs used `C:\Python\Python314\python.exe` (CPython 3.14.7),
the same pyperformance runner, and the same machine. The benchmark results are
noisy and short; the comparison is directional evidence, not a claim of a
reliable regression.

| Case | Shortcut disabled | Shortcut enabled | Enabled vs disabled |
|---|---:|---:|---:|
| `scimark_lu` | 289 ms | 304 ms | 1.05x slower |
| `scimark_fft` | 979 ms | 1.01 s | not significant |
| `scimark_monte_carlo` | 526 ms | 525 ms | not significant |
| `scimark_sor` | 863 ms | 856 ms | not significant |
| `scimark_sparse_mat_mult` | 17.6 ms | 18.1 ms | not significant |

The `isinstance` shortcut therefore does not explain r4's LU result: the
disabled control is still near the r4 candidate's 303 ms, while the older r3
binary took 1.02 s. The cause is the separate guarded integer-branch inline
for eligible Python `__getitem__` bodies, documented in the
[SciMark LU root-cause report](scimark-arraylist-getitem-call-frame-root-cause-20261011.md).
That path recognizes the existing method IR and avoids creating a nested
Python frame for its proven `return self.data[index]` branch; it retains
ordinary Python execution when its guards do not prove equivalence. This is
VM call-site execution of Python IR, not a C++ rewrite of the pure-Python
library and not a general IR optimizer pass. The full r4 result independently
lands at 303 ms, consistent with the earlier source-matched inline trial's
298 ms. The enabled/disabled difference here only measures the unrelated
`isinstance` shortcut. LU is still about 4.0x slower than CPython's 75.3 ms.

## Reproduction artifacts

- [Shortcut-disabled control pyperf JSON](data/scimark-isinstance-isolated-control-20261011.pyperf.json)
- [Shortcut-enabled candidate pyperf JSON](data/scimark-isinstance-isolated-candidate-20261011.pyperf.json)
- Compared with `C:\Python\Python314\python.exe -m pyperf compare_to --table <control> <candidate>`.
- Isolated control executable SHA-256: `5BE46B3D4845A79FE68B8DD9D0A7A945257478380B0F3F2ECE05F8935CDF7019`.
- Isolated candidate executable SHA-256: `7D9FB86BB78EA8357107C92E39253E16C9BAE60BBB2235AE358E83C35580DF39`.
- After restoring the shortcut and rebuilding with
  `doc/performance/data/vm-active-code-view-reproduction-build-command-20261009.cmd`,
  the fixed Release executable SHA-256 is
  `CFE24C7917C8BECFE9221199BEBD29E0D32866FEDA594A394F303135082ECC11`; the
  runtime DLL SHA-256 is
  `EF51A5D8FF6507CEE3838CD2F02724CB43F9C4EB865EB84BC147D7E382C0BE4E`.
