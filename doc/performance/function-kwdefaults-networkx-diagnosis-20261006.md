# Live function keyword defaults block NetworkX (2026-10-06)

The full rebuilt Release run failed all three NetworkX definitions before
timing their workloads: `networkx`, `networkx_connected_components`, and
`networkx_k_core`. Their tracebacks reach `networkx.utils.decorators.argmap`
and fail when `_lazy_compile` reads `func.__argmap__`.

NetworkX creates a Python wrapper with a keyword-only default, then updates
`func.__kwdefaults__` in place to make that default reference the wrapper
itself. XLang3's `object_model.cpp` currently constructs a fresh dictionary
on every `__kwdefaults__` read. Assignment also copies dictionary entries into
the function's separate defaults storage. Thus neither mutation of the
returned dictionary nor later mutation of an assigned dictionary reaches the
argument binder. This is a generic function-metadata defect; NetworkX should
continue executing its own Python decorator code.

## Verified small reproduction

Run [the correctness probe](../../benchmarks/diagnostics/function_kwdefaults_live_probe.py)
with each executable:

```powershell
& 'C:\Python\Python314\python.exe' benchmarks\diagnostics\function_kwdefaults_live_probe.py
& '.\build-repro\main-verify-20261006\Release\xlang3.exe' benchmarks\diagnostics\function_kwdefaults_live_probe.py
```

| Check | CPython 3.14.7 | XLang3 Release |
| --- | --- | --- |
| Repeated reads return the same dictionary | True | False |
| In-place mutation reaches the next call | True | False |
| Removing a default makes the argument required | True | False |
| Assignment preserves the assigned dictionary's identity | True | False |
| Later external mutation reaches the next call | True | False |

The [captured output](data/function-kwdefaults-live-probe-20261006.txt)
records both runs. This is a short correctness check, not a speed measurement.
It ran during the existing full-suite process; no performance claim comes
from it.

The fixed executable SHA-256 is
`A36B8CF5087BCC4E5FCF12F2A81BD528B6104A36663DFEB49B80D5B61B33C7E0`;
the runtime DLL SHA-256 is
`828F125B1C6BEE78519F54A3EAA4D5E4F8356C25530ED038FD26A8EC9B06CA4A`.

## Runtime fix and validation still required

The runtime needs one authoritative mutable dictionary shared by introspection,
assignment, and keyword-only default binding. Preserve fast indexed default
binding for functions whose defaults have never been exposed or reassigned;
do not rebuild dictionaries or rescan defaults on every ordinary Python call.
Once exposed, calls must observe additions, replacement values, deletion,
`clear()`, aliases shared between functions, and `__kwdefaults__ = None`.
Changing positional defaults must preserve keyword-only defaults.

This finding proves the dictionary defect and identifies the corresponding
NetworkX decorator operation. It does not prove that fixing it will complete
all NetworkX benchmarks; later generated-code or runtime failures may remain.
After the ongoing all-97 run completes, implement and validate the generic
runtime correction, run the affected official NetworkX cases, and pass the
complete fixed Release regression gate before committing an engine change.
The live benchmark executable must stay unchanged until that run finishes.
