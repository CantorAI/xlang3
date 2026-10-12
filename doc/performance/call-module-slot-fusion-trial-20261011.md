# Module-slot call fusion trial — 2026-10-11

## Change

The trial folded an adjacent `LoadModuleSlot` and `Call` into an appended
`CallModuleSlot` IR opcode. The opcode performed the ordinary live module-slot
read and then entered the existing guarded `CALL` implementation. This was a
general IR/VM experiment for Python code, not a native implementation of a
standard-library module.

The semantic fixture used during the trial covered global rebinding, a
non-callable replacement, deletion, and restoring the callable after the
missing-name case. The fixture and opcode were removed after profiling showed
that the optimization did not cover the benchmark's hot call sites.

## Evidence

The required Release build command
`doc/performance/data/vm-active-code-view-reproduction-build-command-20261009.cmd`
completed successfully in `build-repro/main-verify-20261006/Release`. The
Python 3.14.7 fixture runner passed all cases:

```powershell
C:\Python\Python314\python.exe tests\run_fixtures.py build-repro\main-verify-20261006\Release\xlang3.exe
```

In fast-mode pyperformance 1.14.0, `typing_runtime_protocols` measured
2.83 ms on this candidate. `pyperf compare_to` measured the preserved fixed
candidate at 2.88 ms and this candidate at 2.83 ms (1.02x faster), but opcode
counts on the official `bench_protocols(500)` body show `CallModuleSlot` ran
only five times while `Call` ran 926,468 times. The rewrite only matched a
module-slot load immediately adjacent to its call; the hot Python call shape
loads/evaluates arguments between those operations. Therefore the timing delta
cannot be attributed to this optimization and should be treated as noise. The
candidate remained 22.67x slower than the saved CPython 3.14.7 sample (125 us).

Raw candidate data: `data/typing-runtime-protocols-call-module-slot-fusion-fast-20261011.json`.
Fixed XLang3 comparison sample: `data/pyperformance-typing-runtime-protocols-current-fixed-20261010-r1.json`.
Saved CPython sample: `data/pyperformance-typing-runtime-protocols-cpython314-20260929.json`.
Opcode profile: `data/typing-protocol-opcounts-call-module-slot-fusion-20261011.log`.
Trial executable SHA-256: `E40CA793A015EF8E39EBFF636DCCC60C95421ADD6457A20BA04EDF293CB68A8B`.

## Assessment

The implementation and its focused fixture have been removed: the candidate
barely exercised the new opcode in the target workload, so the measured 2%
change was not a demonstrated gain. This trial does not reduce the full-suite
gap. The opcode profile and Python 3.14 disassembly point to the repeated
helper calls and attribute/static lookup operations inside `typing`'s unchanged
Python protocol checks as the next area to investigate.
