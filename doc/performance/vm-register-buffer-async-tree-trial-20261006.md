# VM register inline-capacity trial on async-tree (2026-10-06)

## Result

Reducing the inline VM register-buffer capacity from 128 Values to 32 did not
produce a significant improvement on the official pyperformance 1.14.0
`async_tree_none` case. The candidate measured **4.43 s ± 0.04 s** and the
saved fixed Release control measured **4.47 s ± 0.12 s**. `pyperf
compare_to` hid the pair as statistically insignificant. The candidate is
rejected and the source capacity was restored to 128.

| Build | `async_tree_none` mean ± standard deviation | Speed relative to control |
|---|---:|---:|
| Fixed Release control | 4.47 s ± 0.12 s | 1.000× |
| 32-register inline candidate | 4.43 s ± 0.04 s | 1.009× (not significant) |

Both runs had 20 measured values across 10 worker processes, one warmup per
worker, and one loop per value. The control emitted pyperf's instability
warning; its maximum sample was 4.96 s against a 4.45 s median. The candidate
median was 4.43 s. Keep this result as a rejection, not a claimed 0.9% gain.

Against the same-day CPython 3.14.7 `async_tree_none` reference of 226 ms,
the candidate is about **0.051× CPython speed** (19.6× slower). This capacity
change does not materially close the gap.

## Rationale and conclusion

`XlangVMFrame` embeds 128 register Values in its small-buffer storage. The
native async-tree sample pointed to frame creation and cleanup as possible
costs. An earlier 8/8/32 locals/cells/registers trial was rejected on
deepcopy and unpickle. This follow-up changed only register inline capacity
and measured the coroutine-heavy benchmark that had not been used in that
trial. Its result gives no reason to retain a smaller buffer. Do not repeat
this capacity-only experiment without new evidence that changes its cost
profile.

No source optimization or fixture is retained from this trial. The fixed
control binaries remain available in the ignored trial directory; the
committed JSON files preserve the pyperf samples.

## Raw results and build identities

- [Control pyperf JSON](data/register-buffer-async-tree-control-fast-20261006.json)
- [Candidate pyperf JSON](data/register-buffer-async-tree-candidate-fast-20261006.json)

| Build | Executable SHA-256 | Runtime DLL SHA-256 |
|---|---|---|
| Fixed Release control | `244E628BF8A25BCE591F8C07DF1F9F95361BAB1BA41BE318359FCDE3AB1A5548` | `0812BFEF8765E4C0D804437A7DBD2090484398F87263BFC04662EE211FEEBE2D` |
| 32-register candidate | `981731A11DF91BE868D773C34DA29AEE83CFA9F2BDE62E3AB709D53A3FEA4939` | `9D31F1A4886A349570AD7F910C891CB3ED5E7AB6D7009ED25BAB11CF6AE1691E` |

Both used CPython **3.14.7**, pyperformance **1.14.0**, the repository's
Windows pyperf compatibility shim, and the same dependency site. The candidate
changed only `XlangVMSmallRegisterBuffer`'s inline capacity. The ordinary
Release control was preserved before building the candidate. The source was
then restored and rebuilt; the rebuilt artifacts differ in hash from the
preserved binaries, so the preserved pair remains the fixed benchmark
baseline.

Reproduction uses `benchmarks/diagnostics/run_pyperformance_xlang3_shimmed.py`
with `--benchmarks async_tree --mode fast --case-timeout-override
async_tree=600`, the runtime paths named in the table, and
`venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages` as
`--dependency-site`.
