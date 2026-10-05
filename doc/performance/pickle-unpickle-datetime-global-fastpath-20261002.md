# Native `_pickle.loads` date-global fast path (2026-10-02)

The full pyperformance run measured XLang3 `unpickle` at **775 µs**, versus
**10.6 µs** for the saved CPython 3.14.7 result. Profiling the unchanged
official workload showed that every `_pickle.loads` call rejected its native
decode and entered `pickle.py`'s Python opcode loop. The payload's date reducer
uses `_pydatetime.date`, while the native reader's safe-global whitelist only
accepted `datetime.date`.

The `_pickle` native reader now permits either module spelling for the `date`
global, then resolves it and checks that it is the exact `datetime.date` class
before constructing anything. The later `REDUCE` check retains the same
identity requirement. Other globals, custom reducers, and unsupported payloads
continue through the original Python loader. This is an optimization within
XLang3's counterpart to CPython's native `_pickle` module; `pickle.py` and its
`unpickle_pure_python` benchmark remain Python.

On the rebuilt Release candidate, the focused official pyperformance
`unpickle` run measured **77.3 µs**. The later full-suite run confirmed
**75.8 µs**, about **10.2× faster** than the previous full-suite XLang3 result,
and still **7.16× slower** than the saved CPython reference.
`unpickle_pure_python` measured **3.69 ms** in the full run (3.35 ms in a
separate focused run); the earlier full-suite result was 3.63 ms. These
fast-mode comparisons are directional, not paired. Samples warn that their
counts are unstable.

The opt-in `XLANG3_PICKLE_TRACE=1` diagnostic confirms that the three workload
payloads take the native decode path (`decoded=1`). The [trace and runtime
counters](data/pickle-unpickle-native-path-trace-20261002.txt) are diagnostic
only; their direct-run timing is not a pyperf score. The existing
[`pickle_module.py` fixture](../../tests/fixtures/core/pickle_module.py) passes,
including shared `datetime.date` round-tripping. XLang3-to-CPython and
CPython-to-XLang3 protocol-4 interoperability also passes with the accessible
Python 3.13 executable.

## Reproduction and build identity

- Candidate pyperf JSON: [native `unpickle`](data/pickle-unpickle-global-fast-candidate-20261002.json)
- Candidate pyperf log: [native `unpickle` log](data/pickle-unpickle-global-fast-candidate-20261002.log)
- Candidate pyperf JSON: [pure-Python `unpickle_pure_python`](data/pickle-unpickle-pure-python-control-fast-20261002.json)
- Candidate pyperf log: [pure-Python log](data/pickle-unpickle-pure-python-control-fast-20261002.log)
- Prior full-suite XLang3 score: [all-subtest CSV](data/pyperformance-xlang3-decimal-zero-full-fast-20261002-subtests.csv)
- Updated full-suite confirmation: [all-subtest CSV](data/pyperformance-xlang3-pickle-fast-full-fast-20261002-subtests.csv)
- Updated full-suite report: [all 97 cases](pyperformance-xlang3-pickle-fast-full-fast-20261002.md)
- CPython reference: [CPython 3.14.7 full-suite JSON](data/pyperformance-cpython314-clean-release-full-fast-20261002.json)
- Release executable SHA-256: `0C3C75C0CBD7229900D89A7F9F50365B4E35472BC251B750AF297B6CF126B11C`
- Runtime DLL SHA-256: `801FE02438C1ED166ABB212C70527A5D055EB01D79CE2820A867DF2AC1226D49`

The fixed-baseline gate could not run to completion: the preserved baseline
executable fails during startup against the accessible Python 3.13 standard
library (`bytearray.copy` is missing). The candidate passed the focused pickle
fixture and cross-runtime interoperability test. CTest's registered pickle
interop test also could not select Python 3.14 because that executable returns
Access Denied; the equivalent test passed when invoked with Python 3.13. The
full fixture runner currently has an unrelated `runtime_protocol_fastcheck`
expected-output mismatch. These environment and broader-suite issues are
preserved rather than counted as performance results.

This closes one material `unpickle` gap but not the overall speed objective.
