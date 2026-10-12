# Asyncio self-false guard inline trial — 2026-10-05

## Result

I tested a guarded VM fast path for the no-error branch of a Python method
whose IR reads one direct `self` attribute, branches on false, and returns
`None`. The target was `BaseEventLoop._check_closed()`, which appeared 421
times in the small four-level async-tree call profile. The fast path accepted
only an exact `False` stored in the instance attribute vector. Descriptors,
custom truth conversion, hooks, public `__dict__` overlays, observed execution,
and the raising branch used the original Python frame.

The matched official pyperformance 1.14.0 `async_tree_none` fast runs measured
**4.41 ± 0.03 s** for current main and **4.45 ± 0.02 s** for the candidate.
`pyperf compare_to` reports the candidate as **1.01× slower**. Against the
saved CPython 3.14.7 result of **227.4 ms**, the candidate remained about
**19.6× slower**. This did not demonstrate an end-to-end gain, so the VM
change and its fixture were removed.

The attempted rigorous control exceeded the 600-second per-case cap and
produced no score. The reported A/B therefore uses two official fast-mode
distributions with the same runner, dependencies, and runtime configuration.

## Correctness and evidence

The candidate fixture matched CPython 3.14.7 for direct `False`, custom
`__bool__`, integer false, a property descriptor, subclass and instance
method overrides, profile visibility, and the `_check_closed` traceback on
the raising path. The full fixture suite and `xlang3_interpreter_tests`
passed with the candidate. No fixed Release gate was run because the
optimization was rejected and no engine change is being retained.

- [Current-main control pyperf JSON](data/pyperformance-xlang3-async-tree-false-guard-control-fast-20261005.json)
- [Candidate pyperf JSON](data/pyperformance-xlang3-async-tree-self-false-guard-candidate-fast-20261005.json)
- [CPython 3.14.7 full-run reference](data/pyperformance-cpython314-clean-release-full-fast-20261002.json)
- [Call profile identifying `_check_closed`](async-tree-call-profile-20261005.md)

| Build | Executable SHA-256 | Runtime DLL SHA-256 |
|---|---|---|
| Current-main control | `84DD7D0A2B90CFA4A1369F507E50FBDB18B85F7A498CA958771648FA4A1971DE` | `6CCA3D95F4A6CFEF94B50D68EA0873F2EA379A2094DA342743878C05BB4AFEDA` |
| Rejected candidate | `F0F4D8EE10544684C64D1A96643D7AEC762BC53AD32AA0185F3AE1DC39AD8265` | `BE9B6BC7874DC3CF2172B8A3652D7437A81EE962D76752BAAEEBABEDC9708A3F` |

Both pyperformance runs used Python **3.14.7**, pyperformance **1.14.0**,
the repository's Windows pyperf compatibility shim, and the shared dependency
site. No Python 3.13 runtime or standard-library overlay was used.

Reproduction:

```powershell
$env:PYTHONPATH = (Resolve-Path benchmarks\diagnostics\pyperf_compat).Path
& 'venv\cpython3.14-a6792301b742-compat-31b33d68c68a\Scripts\python.exe' `
  benchmarks\diagnostics\run_pyperformance_xlang3_shimmed.py `
  --runtime build-repro\Release\xlang3.exe --benchmarks async_tree `
  --mode fast --case-timeout-override async_tree=600 `
  --output build-repro\pyperformance-async-tree-self-false-guard-candidate-fast.json `
  --dependency-site venv\cpython3.14-a6792301b742-compat-31b33d68c68a\Lib\site-packages
```

```powershell
& 'venv\cpython3.14-a6792301b742-compat-31b33d68c68a\Scripts\python.exe' `
  -m pyperf compare_to --table `
  build-repro\pyperformance-async-tree-current-main-guard-control-fast.json `
  build-repro\pyperformance-async-tree-self-false-guard-candidate-fast.json
```
