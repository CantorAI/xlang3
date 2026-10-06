# `_contextvars.Context.run` active-map pointer trial (2026-10-05)

## Result

The change is retained as a small, measured improvement and a context-entry
correctness fix. On the official pyperformance 1.14.0 `async_tree_none`
benchmark, a rigorous control run measured **5.11 s ± 0.07 s** and the
candidate measured **5.07 s ± 0.04 s**. `pyperf compare_to` reports the
candidate **1.01× faster**. Requiring a full 1% minimum hides the result, so
this is a modest gain rather than a material closure of the performance gap.

Fresh CPython **3.14.7** measured **225 ms ± 10 ms** on the same case, with a
pyperf stability warning. The candidate remains **22.54× slower** than
CPython. The broad objective is still open.

## Change

`Context.run` is invoked for every asyncio Handle callback. Before this trial,
the native `_contextvars` implementation switched contexts with four
`unordered_map::swap` calls per invocation. It now switches a thread-local
pointer to the active context map, matching CPython's current-context pointer
model and leaving each context's map in place. An RAII guard restores the
previous pointer and clears the entered marker on all returns. An atomic
entered marker rejects nested or concurrent entry of the same Context; a new
fixture checks same-context re-entry, while existing fixtures cover nested
different contexts, callback arguments, keywords, and exception restoration.

The code comment records why the pointer is used: `Context.run` sits on the
asyncio callback hot path, and copying or swapping the maps adds repeated work
to every callback. The native module remains `_contextvars`, whose CPython
counterpart is native; `contextvars.py` remains Python code.

## Official benchmark data

All runs used pyperformance 1.14.0 on Windows 11, the same Python 3.14.7
dependency site, and `--rigorous` mode. The XLang3 control and candidate each
recorded 40 measurement rounds (120 values). The CPython reference also used
`C:\Python\Python314\python.exe` 3.14.7.

| Runtime | Mean ± standard deviation | Ratio vs CPython 3.14.7 |
|---|---:|---:|
| CPython 3.14.7 | 225 ms ± 10 ms | 1.00× |
| XLang3 fixed Release control | 5.11 s ± 0.07 s | 22.73× slower |
| XLang3 pointer candidate | 5.07 s ± 0.04 s | 22.54× slower |

The strict comparison command was:

```powershell
C:\Python\Python314\python.exe -m pyperf compare_to `
  doc\performance\data\async-tree-none-control-pointertrial-rigorous-20261005.json `
  doc\performance\data\async-tree-none-candidate-pointertrial-rigorous-20261005.json `
  --table --verbose
```

Raw data and logs:

- [XLang3 control JSON](data/async-tree-none-control-pointertrial-rigorous-20261005.json) and [log](data/async-tree-none-control-pointertrial-rigorous-20261005.log)
- [XLang3 candidate JSON](data/async-tree-none-candidate-pointertrial-rigorous-20261005.json) and [log](data/async-tree-none-candidate-pointertrial-rigorous-20261005.log)
- [CPython 3.14.7 JSON](data/async-tree-none-cpython3147-pointertrial-rigorous-20261005.json) and [log](data/async-tree-none-cpython3147-pointertrial-rigorous-20261005.log)

## Diagnostic and validation

The full official `async_tree_none` shape creates 55,987 recursive tasks.
The XLang3 `--perf-counters` diagnostic counted 1,159,243 native calls,
65,322 generic and 55,992 fast `_contextvars.Context.run` calls, and
17,186,001 IR opcode dispatches. The counter run is not a timing result; it
confirms that the changed path executes at the workload's full scale. The
[counter output](data/async-tree-official-none-xlang3-counters-20261005.txt)
can be regenerated with:

```powershell
$env:XLANG3_PYTHON_LIB = 'C:\Python\Python314\Lib'
$env:PYTHONPATH = 'benchmarks\diagnostics;benchmarks\diagnostics\pyperf_compat;C:\Python\Python314\Lib;C:\Python\Python314\Lib\site-packages'
.\build-repro\Release\xlang3.exe --perf-counters benchmarks\diagnostics\async_tree_official_once.py none
```

Correctness and regression checks:

- Full Release CTest: **54/55 passed**. The remaining
  `xlang3_cli_visual_studio_debugpy_launch` failure is the existing assertion
  that its Visual Studio profile does not use `xlang3.exe` directly.
- The complete fixture test passed, including the new
  `context_run_same_context_reentry` case and the existing Context.run cases.
- The full default 11-case Release regression gate passed against the
  preserved `build-repro\perf-control` runtime; its worst ratio was
  `local_slots` at **1.040×**, below the 1.10 threshold. See the
  [gate JSON](data/context-run-pointer-fixed-release-gate-20261005.json).

| Build | `xlang3.exe` SHA-256 | `xlang3_runtime.dll` SHA-256 |
|---|---|---|
| Fixed Release control | `091105B9328CC1D1531E2E70FB86B9FDC23A8608E3BE8DDCBAD90BDB532A8B3F` | `27E2892713E8F473613F73B9E658B4C353366197068059AFE2F9B0E36C4A1F94` |
| Pointer candidate | `091105B9328CC1D1531E2E70FB86B9FDC23A8608E3BE8DDCBAD90BDB532A8B3F` | `F03673BFD9CE90D77631B3C8EDB267802FD4D0BB0489498D80E9791BB9B3E038` |
