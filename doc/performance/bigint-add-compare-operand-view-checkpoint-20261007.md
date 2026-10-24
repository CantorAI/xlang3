# Rejected read-only operand helper trial — 2026-10-07

**Rejected engine trial.** Official `pidigits`: previous XLang3 **246.397 ms**, candidate **287.897 ms**, CPython **3.14.7 176.515 ms**. Nominal candidate gain is **0.856×**, or **-16.84% less time**. Relative speed versus CPython is **0.613×**, with CPython at 1×. XLang3 still takes **1.631×** CPython's time.

![Official pidigits speed relative to CPython 3.14.7](bigint-add-compare-operand-view-checkpoint-20261007.svg)

## Source-backed reason and implementation

XLang3 cloned both integer operands before addition or comparison. Subtraction then cloned the right operand again to negate it. This imposed allocator and copy costs even when comparison could finish from signs or limb counts. The existing immutable `BigIntOperandView` now serves addition, subtraction and comparison when both operands are intrinsic Int64/Bool/BigInt values. Addition/subtraction allocate independent owned results; subtraction flips only the local view sign. Comparison reads the immutable limbs and publishes its bool after all reads.

CPython 3.14.7 also reads input digits directly: [comparison](https://github.com/python/cpython/blob/v3.14.7/Objects/longobject.c#L3322) and [addition/subtraction](https://github.com/python/cpython/blob/v3.14.7/Objects/longobject.c#L3440). This applies the same ownership principle to XLang3’s own generic arithmetic; no CPython pure-Python library algorithm was translated into C++.

If either input requires wrapper conversion, the original ordered owned conversion remains. A right-input hook can destroy the original left-input owner; the left snapshot must survive. Durable comments explain this fallback, why comparisons should not clone, why subtraction negates only a view, and why outputs must be independent before replacing input owners. The read-only view type remains distinct from mutable owned payloads.

## Correctness and fixed gate

Passed **364 core fixtures, 11 compatibility sections, 3 expected failures** and **8 C++/SDK/graph tests**. Forty signed C++ reference cases cover 8 operations and 3 output modes (**960 combinations**), including replacing either sole input owner. Separate retained-input checks, destructive conversion reentry for all three entry points, self-subtraction, bools, carry/borrow chains, and signed 64-bit promotion/compaction guard ownership and semantics. The Python fixture independently checks sign cases, six comparisons, equal separately constructed operands, zero, carry/borrow chains, bools and scalar boundaries.

The candidate produces exact CPython 2,000 pi digit bytes: SHA-256 `e7cb4bbb129d29f035c29cf1f088673788a82ebbbd61b466257e12ece4871aac`.

The complete default fixed gate passed with exit 0: **11 cases, 21 paired repeats, 5 warmups, 10% tolerance**. The accepted baseline is unchanged: exe SHA-256 `a5f5028c15e145edce645a5afc25c11fbce77f51e882312b1fbe06e63c72a4af`, DLL `bc1b9c0a8086f7e6fb0c037516dc9c1eea20427fa887e3aa623714bc5ef5da8d`.

## Measurement scope

The original diagnostic may overlap a separate xMind Release build and is retained only as historical diagnostic evidence. That build was observed live, then its process disappeared; a fresh `idle-r2` before diagnostic supplies the scored comparison. No build or other benchmark ran during subsequent controlled measurements.

Official previous-build, candidate and CPython runs share benchmark source, dependencies and compatibility hook. XLang3 ran at the unchanged path `D:\CantorAI\xlang3\build-repro\main-verify-20261006\Release\xlang3.exe` for both engine versions. The preserved previous DLL was temporarily restored there for before, and the candidate restored before after and again in controller cleanup. Module hashes matched and every phase verified unchanged executable/DLL hashes.

Official results are independent sequential fast-mode samples with stability warnings, not a paired significance test. Only affected official `pidigits` was rerun. The historical 97-case full-suite data, failures and aggregate remain unchanged; these targeted samples are not spliced into it.

## Isolated operator measurements

Median of five samples with **50,000 operations per sample**, including Python loop, assignment and operator dispatch. Equal bigint operands are separately constructed. Gain compares previous XLang3 to candidate; “vs CPython” is CPython time / candidate time, where values above 1× mean faster than CPython.

| Case | Before (µs) | Candidate (µs) | CPython 3.14.7 (µs) | Gain | vs CPython |
|---|---:|---:|---:|---:|---:|
| add-small-128 | 0.399 | 0.311 | 0.025 | 1.284× | 0.081× |
| add-big-128 | 0.450 | 0.376 | 0.025 | 1.195× | 0.066× |
| add-opposite-128 | 0.383 | 0.315 | 0.024 | 1.216× | 0.075× |
| subtract-small-128 | 0.400 | 0.302 | 0.023 | 1.322× | 0.075× |
| subtract-big-128 | 0.399 | 0.298 | 0.021 | 1.340× | 0.070× |
| subtract-opposite-128 | 0.471 | 0.377 | 0.025 | 1.248× | 0.067× |
| compare-small-128 | 0.197 | 0.116 | 0.016 | 1.690× | 0.141× |
| compare-big-128 | 0.203 | 0.114 | 0.020 | 1.779× | 0.172× |
| compare-equal-128 | 0.216 | 0.128 | 0.018 | 1.686× | 0.142× |
| add-small-1024 | 0.448 | 0.350 | 0.036 | 1.279× | 0.101× |
| add-big-1024 | 0.540 | 0.420 | 0.035 | 1.284× | 0.084× |
| add-opposite-1024 | 0.475 | 0.384 | 0.029 | 1.237× | 0.075× |
| subtract-small-1024 | 0.469 | 0.328 | 0.038 | 1.427× | 0.114× |
| subtract-big-1024 | 0.501 | 0.347 | 0.029 | 1.442× | 0.085× |
| subtract-opposite-1024 | 0.560 | 0.415 | 0.037 | 1.349× | 0.090× |
| compare-small-1024 | 0.205 | 0.117 | 0.018 | 1.756× | 0.152× |
| compare-big-1024 | 0.234 | 0.121 | 0.027 | 1.925× | 0.221× |
| compare-equal-1024 | 0.248 | 0.146 | 0.026 | 1.697× | 0.178× |
| add-small-4096 | 0.536 | 0.425 | 0.112 | 1.261× | 0.264× |
| add-big-4096 | 0.638 | 0.506 | 0.110 | 1.262× | 0.218× |
| add-opposite-4096 | 0.627 | 0.510 | 0.064 | 1.229× | 0.126× |
| subtract-small-4096 | 0.550 | 0.403 | 0.136 | 1.363× | 0.337× |
| subtract-big-4096 | 0.641 | 0.486 | 0.062 | 1.317× | 0.128× |
| subtract-opposite-4096 | 0.652 | 0.487 | 0.110 | 1.338× | 0.225× |
| compare-small-4096 | 0.221 | 0.112 | 0.017 | 1.969× | 0.152× |
| compare-big-4096 | 0.278 | 0.152 | 0.062 | 1.825× | 0.406× |
| compare-equal-4096 | 0.288 | 0.172 | 0.063 | 1.672× | 0.364× |

## Raw evidence

[Binary/source/input hash checkpoint](data/bigint-add-compare-operand-view-checkpoint-20261007.json).

- [pyperformance-bigint-add-compare-operand-view-pidigits-before-fast-20261007.json](data/pyperformance-bigint-add-compare-operand-view-pidigits-before-fast-20261007.json)
- [pyperformance-bigint-add-compare-operand-view-pidigits-before-fast-20261007.log](data/pyperformance-bigint-add-compare-operand-view-pidigits-before-fast-20261007.log)
- [pyperformance-bigint-add-compare-operand-view-pidigits-before-fast-20261007-provenance.json](data/pyperformance-bigint-add-compare-operand-view-pidigits-before-fast-20261007-provenance.json)
- [pyperformance-bigint-add-compare-operand-view-pidigits-after-fast-20261007.json](data/pyperformance-bigint-add-compare-operand-view-pidigits-after-fast-20261007.json)
- [pyperformance-bigint-add-compare-operand-view-pidigits-after-fast-20261007.log](data/pyperformance-bigint-add-compare-operand-view-pidigits-after-fast-20261007.log)
- [pyperformance-bigint-add-compare-operand-view-pidigits-after-fast-20261007-provenance.json](data/pyperformance-bigint-add-compare-operand-view-pidigits-after-fast-20261007-provenance.json)
- [pyperformance-bigint-add-compare-operand-view-pidigits-cpython3147-fast-20261007.json](data/pyperformance-bigint-add-compare-operand-view-pidigits-cpython3147-fast-20261007.json)
- [pyperformance-bigint-add-compare-operand-view-pidigits-cpython3147-fast-20261007.log](data/pyperformance-bigint-add-compare-operand-view-pidigits-cpython3147-fast-20261007.log)
- [pyperformance-bigint-add-compare-operand-view-pidigits-cpython3147-fast-20261007-provenance.json](data/pyperformance-bigint-add-compare-operand-view-pidigits-cpython3147-fast-20261007-provenance.json)
- [bigint-add-compare-diagnostic-before-idle-r2-20261007.json](data/bigint-add-compare-diagnostic-before-idle-r2-20261007.json)
- [bigint-add-compare-diagnostic-after-20261007.json](data/bigint-add-compare-diagnostic-after-20261007.json)
- [bigint-add-compare-diagnostic-before-20261007.json](data/bigint-add-compare-diagnostic-before-20261007.json)
- [bigint-add-compare-operand-view-validation-20261007.json](data/bigint-add-compare-operand-view-validation-20261007.json)
- [bigint-add-compare-operand-view-validation-20261007-fixtures.log](data/bigint-add-compare-operand-view-validation-20261007-fixtures.log)
- [bigint-add-compare-operand-view-validation-20261007-cpp.log](data/bigint-add-compare-operand-view-validation-20261007-cpp.log)
- [release-bigint-add-compare-operand-view-fixed-gate-20261007.json](data/release-bigint-add-compare-operand-view-fixed-gate-20261007.json)
- [release-bigint-add-compare-operand-view-fixed-gate-20261007.log](data/release-bigint-add-compare-operand-view-fixed-gate-20261007.log)
- [pidigits-add-compare-operand-view-body-correctness-20261007.json](data/pidigits-add-compare-operand-view-body-correctness-20261007.json)

Seven alternating body comparisons on CPU [0] confirmed the slowdown: previous/candidate ratios **0.872–0.920×**. These diagnostics verify exact digit hashes and are not official suite scores. The first trial DLL and source remain preserved; its engine change was revised before commit.

[Paired evidence](data/bigint-add-compare-paired-body-followup-20261007.json); [original source diff](data/bigint-add-compare-initial-trial-source-20261007.patch); [other-kernel followup](data/bigint-add-compare-other-kernel-followup-20261007.json).
