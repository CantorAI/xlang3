# Prepared VM metadata cache trial (2026-09-30)

The candidate was discarded. Reusing immutable preparation state made the
official pure-Python unpickle benchmark about 1% faster, but the complete local
suite was mixed and four cases had small slowdowns whose paired intervals
excluded 1.0. The committed Release executable and runtime were restored.
This trial does not establish a material reduction of the CPython gap.

## Hypothesis and candidate

`XlangVMFrame::reset()` already preserves instruction-cache storage for
functions previously used at that stack depth. However, switching back to one
of those functions still calls `compute_register_last_use()`, which acquires
the function's immutable metadata through an atomic shared-pointer load, and
`reserve_call_args()`, which scans all positional call-argument tables.

The candidate moved the active `execution_metadata` pointer into the existing
prepared-function entry, together with the maximum positional argument count.
It restored both on a cache hit, checked `metadata->owner == fn`, and reserved
the previously calculated argument capacity. First activations and mismatched
metadata retained the original preparation path. The cache's module ownership,
32-entry limit, frame cleanup, tracing, monitoring, and argument binding were
unchanged. The [saved patch](data/prepared-metadata-trial-20260930.patch)
includes the design comment explaining the function-identity invariant.

This is a shared VM experiment. CPython's pure-Python library implementations,
including `pickle.py` and `copy.py`, remained Python code. No library algorithm
was implemented as a C++ module.

## Official pyperformance result

Both XLang3 runs used pyperformance 1.14.0's unchanged
`bm_pickle/run_benchmark.py`, pyperf 2.10.0, `--rigorous --pure-python
--protocol 5 unpickle`, and separate worker cache directories. The same
`pyperf_compat` shim disabled unsupported Windows priority and host-metadata
hooks. Worker sampling, calibration, and the timed benchmark body stayed in
use.

| Runtime | Mean ± standard deviation | Speed with CPython = 1.0× |
|---|---:|---:|
| CPython 3.14.7, earlier same-day rigorous reference | 0.1616 ± 0.0025 ms | 1.000× |
| XLang3 unchanged parent | 4.3614 ± 0.0606 ms | 0.037× |
| XLang3 discarded candidate | 4.3132 ± 0.0585 ms | 0.037× |

Speed is CPython elapsed time divided by XLang3 elapsed time. The parent takes
**26.99× longer** than this CPython reference. The candidate takes 26.69×
longer. The CPython reference was not rerun for this experiment.

`pyperf compare_to -v` reports **1.01× faster**, significant at **t = 6.27**.
The measured time reduction is 1.11%; it is a small isolated result.

Elapsed time, with shorter bars indicating faster execution:

```text
CPython 3.14.7 reference    0.162 ms |█
XLang3 parent              4.361 ms |███████████████████████████
Discarded candidate        4.313 ms |███████████████████████████
```

Raw runs: [parent](data/unpickle-prepared-metadata-parent-rigorous-20260930.json),
[candidate](data/unpickle-prepared-metadata-candidate-rigorous-20260930.json),
and [earlier CPython reference](data/unpickle-singlebyte-control-cpython314-rigorous-20260930.json).

## Complete local comparison and decision

The complete default 11-case gate ran against the immediately preserved
parent. It used 21 order-balanced samples per case after five warmups and
passed with exit 0. Passing the 10% release tolerance does not make every
small slowdown acceptable as an optimization.

Ratios below are candidate elapsed time divided by parent elapsed time;
values below 1.0 are faster. Intervals come from the paired gate report.

| Case | Elapsed ratio | Paired 95% interval |
|---|---:|---:|
| `local_slots` | 0.970× | 0.965–0.977× |
| `scalar_arithmetic` | 0.999× | 0.991–1.009× |
| `range_for` | 0.982× | 0.974–1.002× |
| `function_calls` | 1.022× | 0.995–1.035× |
| `class_construct` | 1.004× | 1.0003–1.009× |
| `list_append` | 1.002× | 0.964–1.021× |
| `property_access` | 1.014× | 1.005–1.021× |
| `deepcopy_memo` | 0.991× | 0.987–0.996× |
| `json_dumps` | 1.009× | 1.006–1.011× |
| `gc_traversal` | 0.997× | 0.990–1.010× |
| `subparsers` | 1.013× | 1.012–1.016× |

The `function_calls` point estimate is slower, but its interval includes 1.0.
The intervals for `class_construct`, `property_access`, `json_dumps`, and
`subparsers` support small slowdowns. The changed frame layout and generated
native code may also
affect cases that do not frequently switch functions; these measurements do
not isolate a causal explanation for each case.

The broad results do not justify retaining the candidate to address the large
VM gap. The source change and candidate binaries were removed. The next
investigation should attribute native time inside dispatch and frame handling,
rather than repeat this preparation-cache shortcut.

The [complete gate JSON](data/prepared-metadata-immediate-parent-gate-20260930.json)
retains every sample, source hash, runtime identity, and interval. The full
fixture runner also passed with exit 0: 300 core fixtures, 11 compatibility
sections, and three expected-failure checks. A candidate check against the
older fixed accepted baseline was not needed after rejection; no engine
change from this trial is committed or claimed as release validated.

## Build identity and reproduction

The parent source was `f478d77`, with no tracked source changes. The candidate
is the saved patch applied to that revision with `git apply --unidiff-zero`.
Both were built as Release with the existing build configuration.

| Binary | Parent / restored | Candidate |
|---|---|---|
| `xlang3.exe` | `2E221C1EBCA6E5D3560D530A08BAA2384FBFD49B9D0FB2250D6CD80D86410D27` | `A0BF36E1645D2745C3271E7387EC332F2AAB86CB858A024AAEDCD0C7EC5C3C8C` |
| `xlang3_runtime.dll` | `5A4A9A236C7426E1D2BD5430C01DB97487B2E82D33719AC2D9A2A9A28C994CC6` | `FFB76E5B9C1A280F8B70FB3551AFE471EFF8D5030BA8BD8B3804CADA61BE8511` |

Set `PYTHONPATH` to `benchmarks/diagnostics/pyperf_compat` and the installed
benchmark dependency environment's `Lib/site-packages`; use a separate
`PYTHONPYCACHEPREFIX` for each binary. The benchmark command on this host was:

```powershell
C:\Python\Python314\python.exe `
  C:\Python\Python314\Lib\site-packages\pyperformance\data-files\benchmarks\bm_pickle\run_benchmark.py `
  --rigorous --python <preserved-parent-or-candidate-executable> `
  --inherit-environ PYTHONPATH,PYTHONPYCACHEPREFIX `
  --output <result.json> --pure-python --protocol 5 unpickle
```

The candidate correctness and local comparison commands were:

```powershell
C:\Python\Python314\python.exe tests/run_fixtures.py build/Release/xlang3.exe
C:\Python\Python314\python.exe benchmarks/check_regression.py `
  --baseline scratch/performance-prepared-metadata-parent-20260930/xlang3.exe `
  --candidate build/Release/xlang3.exe `
  --output doc/performance/data/prepared-metadata-immediate-parent-gate-20260930.json
```
