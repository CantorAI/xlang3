# Inlining the VM event poll (2026-10-04)

## Result

Rejected. Moving the per-thread event-poll countdown into an inline header
function was intended to remove a cross-translation-unit call from the common
per-opcode VM path. It preserved local weakref delivery and the existing
64-instruction bound for cross-thread events, and all focused weakref, signal,
thread, async, interpreter, and runtime tests passed. The official pyperformance
pair did not justify the change: `pickle_pure_python` measured **5.40 ms ±
0.40 ms** on control and **5.50 ms ± 0.38 ms** on candidate; `unpickle_pure_python`
measured **2.35 ms ± 0.11 ms** and **2.37 ms ± 0.13 ms**. `pyperf compare_to`
reported pickle 1.02x slower and hid unpickle as not significant. The inline
implementation was removed.

The remaining event-poll implementation still checks same-thread weakref
hints immediately and samples cross-thread events every 64 VM instructions.
The diagnostic profile's VM-loop share includes more than this helper; this
trial shows that removing this one call does not improve the representative
pickle workloads.

## CPython reference and evidence

Against the saved CPython **3.14.7** pyperformance 1.14.0 run, the inline
candidate was 20.07x slower on pickle and 11.56x slower on unpickle. Both XLang3
runs used the same 3.14.7 benchmark manager, dependency site, and `--rigorous`
mode.

- [Control rigorous pyperf JSON](data/inline-event-poll-control-rigorous-20261004.json)
- [Inline candidate rigorous pyperf JSON](data/inline-event-poll-candidate-rigorous-20261004.json)
- [CPython 3.14.7 full-suite reference](data/pyperformance-cpython314-clean-release-full-fast-20261002.json)

Build hashes:

| Build | Executable SHA-256 | Runtime DLL SHA-256 |
|---|---|---|
| Control (flat integer index) | `0BEE56B3872EB85BABDB78E2545E2E2FF1A3D9DEC09E9279D1200D749A437777` | `98E49CE9FBA27366CC8A18ECC9864EAB9889B522B3DBCEDA92F9C6000EB5C345` |
| Rejected inline candidate | `8AF4FC36310459795C887CBEFEE74C00A3B3EB3FEF36D877E29777D621189B45` | `DCE38A3BFE32B22BE551CC1AC70598F710A0083D7E25C416A09AD596163A0C28` |

The normal Release target was rebuilt at `build-repro/Release/` after removal.
