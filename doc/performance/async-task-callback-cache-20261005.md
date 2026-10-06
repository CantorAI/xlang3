# Async Task callback-cache experiment — 2026-10-05

## Result

I tested storing each native Task's `_step`, `_eager_step`, and `_wakeup`
bound-method wrappers in its native payload. The change included GC tracing for
the cached wrappers and cleared them when the Task completed, since each
bound method owns its Task.

The official `async_tree_none` benchmark measured **4.44 ± 0.02 s** with the
cache and **4.38 ± 0.04 s** immediately before it. `pyperf compare_to` reports
the candidate at **1.01× slower**. Against CPython 3.14.7's saved **227.4 ms**
result, the candidate is about **19.5× slower**. This experiment did not
improve the end-to-end case, so the code change was removed.

The full fixed Release regression gate passed all 11 cases. Its largest
candidate/control median was **1.020×** (`scalar_arithmetic`), within the
10% limit. The complete fixture suite and `xlang3_interpreter_tests` also
passed with the candidate build.

## Reproduction and evidence

Both pyperformance files use pyperformance 1.14.0 in `--fast` mode and the
same XLang3/CPython 3.14.7 environment. The full Release gate uses 21 paired
samples per case and the preserved `build-repro/perf-control/xlang3.exe`
baseline.

- Before-cache XLang3 result:
  [`pyperformance-xlang3-async-tree-before-task-callback-cache-fast-20261005.json`](data/pyperformance-xlang3-async-tree-before-task-callback-cache-fast-20261005.json)
- Candidate XLang3 result:
  [`pyperformance-xlang3-async-tree-task-callback-cache-fast-20261005.json`](data/pyperformance-xlang3-async-tree-task-callback-cache-fast-20261005.json)
- Fixed Release gate:
  [`release-task-callback-cache-gate-20261005.json`](data/release-task-callback-cache-gate-20261005.json)
- CPython 3.14.7 reference:
  [`pyperformance-cpython314-clean-release-full-fast-20261002.json`](data/pyperformance-cpython314-clean-release-full-fast-20261002.json)

The candidate Release executable SHA-256 was
`84DD7D0A2B90CFA4A1369F507E50FBDB18B85F7A498CA958771648FA4A1971DE`; its
runtime DLL SHA-256 was
`3DF0DE4A1CEC567AC1F743653A006805695CB15222A021A2C680B871E5AA809C`.

The benchmark commands were:

```powershell
$env:PYTHONPATH = (Resolve-Path benchmarks\diagnostics\pyperf_compat).Path
& 'venv\cpython3.14-a6792301b742-compat-31b33d68c68a\Scripts\python.exe' `
  benchmarks\diagnostics\run_pyperformance_xlang3_shimmed.py `
  --runtime build-repro\Release\xlang3.exe --benchmarks async_tree `
  --mode fast --case-timeout-override async_tree=600 `
  --output build-repro\pyperformance-async-tree-after-task-callback-cache-fast.json `
  --dependency-site venv\cpython3.14-a6792301b742-compat-31b33d68c68a\Lib\site-packages
```

```powershell
& 'C:\Python\Python314\python.exe' benchmarks\check_regression.py `
  --baseline build-repro\perf-control\xlang3.exe `
  --candidate build-repro\Release\xlang3.exe `
  --output build-repro\release-task-callback-cache-gate.json
```

The comparison used:

```powershell
& 'venv\cpython3.14-a6792301b742-compat-31b33d68c68a\Scripts\python.exe' `
  -m pyperf compare_to --table `
  build-repro\pyperformance-async-tree-before-cache-fast.json `
  build-repro\pyperformance-async-tree-after-task-callback-cache-fast.json
```

The CPython reference benchmark in this report is Python **3.14.7**. No
Python 3.13 interpreter or standard-library overlay was used.
