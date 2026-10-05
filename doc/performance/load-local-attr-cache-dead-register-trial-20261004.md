# LoadLocalAttr dead-temporary cache trial — 2026-10-04

## Result

Rejected for lack of an end-to-end win. The experiment reused the existing
instance attribute inline cache directly from a local on cache hits and skipped
materializing the receiver register only when liveness showed the register was
consumed by that instruction alone. The unchanged official `comprehensions`
benchmark measured 173 µs ± 6 µs on the Release control and 178 µs ± 18 µs on
the candidate. `pyperf compare_to --verbose` hid the difference as not
significant; both fast runs warned that they had not met pyperf's 1% stability
target. No performance gain is established, so the code change was reverted.

## Correctness checks

The Release build succeeded. `xlang3_runtime_value_tests` and
`xlang3_interpreter_tests` passed. Focused fixtures for cached attribute
precedence, custom `__getattribute__`, inherited hooks, nested comprehensions,
generator expressions, nested generator capture, and multiple comprehension
filters passed. The full `run_fixtures.py` attempt stopped at
`ctypes_pointer_return` because this Python installation reports
`NotImplementedError: libffi is unavailable for ctypes`; that run is not
counted as a pass.

## Reproduction data

All runs used pyperformance 1.14.0 and `C:\Python\Python314\python.exe`
(CPython 3.14.7). The same-host CPython comprehension reference measured
14.5 µs ± 1.2 µs in fast mode.

- Control XLang3: 173 µs ± 6 µs; executable SHA-256
  `4E014A32CDB168B1B10965B0BB242824F777DF0E4C9B635FE2435A39B88E6414`, DLL
  `539C434DF8EC85F35CBACEFF4402359C8432FB0B5E7E81B3F4B35A80AD6688BF`.
- Candidate XLang3: 178 µs ± 18 µs; executable SHA-256
  `D2ACAA48D47DD9BA311C783ACEAEA0CD7500864936C71DBAE8D4EFA23E4886AB`, DLL
  `AA3099476CE263DBFEC44C5CF188F36623BE8D153A84B67F996671FBA3991D0F`.
- CPython 3.14.7: 14.5 µs ± 1.2 µs.
- Raw JSON, logs, binary copies, and focused fixture outputs are under
  `scratch/performance-trials/load-local-attr-cache-20261004/`.

The fixed Release binaries at `build-repro/Release` were restored byte-for-byte
from the control. The overall pyperformance objective remains open.
