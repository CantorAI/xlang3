# ListAppend dead-register transfer trial — 2026-10-04

## Result

Rejected. Moving a source register into an exact list when static liveness
marked it dead at `ListAppend` passed a focused finalizer/liveness check, but did
not improve the official `comprehensions` benchmark. One matched fast run was
173 µs ± 6 µs for the saved Release control and 177 µs ± 5 µs for the candidate;
`pyperf compare_to --verbose --table` reported the candidate 1.02× slower.
Both runs warned that fast mode had not reached pyperf's 1% stability target.
The candidate source and fixture were removed, and the fixed Release executable
and DLL were restored byte-for-byte from the saved control.

The hypothesis was that moving a dead appended value would avoid a retain and
release pair. The candidate's per-instruction liveness checks and/or changed
register cleanup outweighed that saving on this workload. Do not retry this
same ListAppend move without a materially different implementation hypothesis.

## Validation and evidence

- The temporary fixture output matched CPython 3.14.7 for a temporary object
  removed immediately after append and for an object still held by a local.
- The comparison used pyperformance 1.14.0 and exactly
  `C:\Python\Python314\python.exe` (CPython 3.14.7).
- Control Release SHA-256: executable
  `4E014A32CDB168B1B10965B0BB242824F777DF0E4C9B635FE2435A39B88E6414`, DLL
  `539C434DF8EC85F35CBACEFF4402359C8432FB0B5E7E81B3F4B35A80AD6688BF`.
- Candidate Release SHA-256: executable
  `B0B129A9933A0D1C745D8D5596A01E90BC0A77A29D2C62345FCFD8D1DC636F21`, DLL
  `A48FF62B7859C16A9D6B6B586F5C9335BC2A17FBC1E2DFD684E3C1250CB88DDB`.
- Raw pyperf results, logs, binary copies, and fixture outputs are under
  `scratch/performance-trials/list-append-dead-register-20261004/`.

The overall speed objective remains open. The next experiment should target
the dominant Python method/generator execution path rather than adding checks to
another frequent container opcode.
