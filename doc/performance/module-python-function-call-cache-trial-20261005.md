# Module Python-function call-cache trial (2026-10-05)

## Result

Rejected: caching a Python function exported from a module did not produce a
measurable `json_dumps` improvement. `pyperf compare_to` marked the rigorous
control/candidate comparison as not significant. Keep the current native
module-call and Python frame behavior unchanged; this lookup is not the main
cause of the roughly 4.3x `json_dumps` gap.

## Candidate

The candidate cached an ordinary `FunctionObject` found in a module slot at a
`CallMethod` site. Cache hits checked the owning module identity and version,
then entered the same Python frame path. Rebinding or deleting a module
attribute increments the module version, which invalidates the cached
function. Native module calls and missing-attribute `__getattr__` behavior
kept their existing paths. The candidate touched only generic VM dispatch; it
did not replace Python library functions with C++ implementations.

## Measurements

All official runs used pyperformance 1.14.0, CPython **3.14.7**, the same
dependency site, and the fixed Release configuration. The sequence was
control, candidate, candidate, control.

| Run | Control | Candidate |
|---|---:|---:|
| Fast 1 | 34.4 ms ± 0.3 ms | 34.1 ms ± 0.4 ms |
| Fast 2 | 35.3 ms ± 5.0 ms | 34.1 ms ± 0.4 ms |
| Rigorous 1 | 34.9 ms ± 3.7 ms | 34.5 ms ± 2.8 ms |

The fast control's second run had 14% standard deviation and pyperf warned
that it was unstable. The rigorous comparison hid the benchmark as not
significant. The instrumented wrapper profile also did not show a clear
effect: five-pair medians were 47.748 ms whole-workload / 36.916 ms in
`JSONEncoder.encode` for control and 46.760 ms / 36.689 ms for candidate.
These diagnostic timings include profiling overhead and are not pyperformance
scores.

The prior full-suite CPython 3.14.7 result for `json_dumps` was 8.063 ms,
versus 34.835 ms for XLang3. This cache does not close that gap.

## Decision and evidence

The source change was removed after the insignificant result. The fixed
`build-repro/Release` path was restored to the control build. The retained
runtime continues to use the existing native `_json` accelerator, which
preserves CPython's `_json` import name and encoder API; the candidate did not
change that module.

- [Control fast run 1](data/json-module-call-cache-control-r1-fast-20261005.json)
- [Candidate fast run 1](data/json-module-call-cache-candidate-r1-fast-20261005.json)
- [Candidate fast run 2](data/json-module-call-cache-candidate-r2-fast-20261005.json)
- [Control fast run 2](data/json-module-call-cache-control-r2-fast-20261005.json)
- [Control rigorous run](data/json-module-call-cache-control-r1-rigorous-20261005.json)
- [Candidate rigorous run](data/json-module-call-cache-candidate-r1-rigorous-20261005.json)
- [Control wrapper profile](data/json-dumps-wrapper-profile-control-20261005.json)
- [Candidate wrapper profile](data/json-dumps-wrapper-profile-candidate-20261005.json)
