# `pickle_pure_python`: current-candidate measurement (2026-10-04)

## Result

The latest scratch candidate does not improve this case over the preserved
Release baseline. Official pyperformance 1.14.0 rigorous runs on Python 3.14.7
measured **5.15 ms ±0.07 ms** for the candidate and **5.12 ms ±0.09 ms** for
the fixed Release build. `pyperf compare_to` reports the candidate at **1.01×
slower**; this is not a meaningful regression or gain. Against the saved
CPython 3.14.7 result of **274 µs**, the current candidate is **18.83× slower**.

The benchmark selects the pure-Python Pickler. Keep `pickle.py` in Python and
address the gap in XLang3's VM/runtime. The existing implementation already
recognizes a constant-return method such as `_Pickler.persistent_id` for
guarded method inlining: the emitted `pickle.py` IR has a direct `ReturnConst`
for that function, and `CallMethod` has an `InlineConstMethod` path. Repeating
that call-cache experiment is therefore not justified by this measurement.

The saved Python call profile is diagnostic only. Its `sys.setprofile` hook
disables Python-function inlining by design, so its 6,080 observed
`persistent_id` calls cannot be treated as normal-build call counts or timing
shares. Use uninstrumented benchmark data to evaluate any follow-up change.

## Reproduction and evidence

Both XLang3 runs used the official `pickle_pure_python` definition from
pyperformance 1.14.0, CPython 3.14.7 as the pyperf manager, the same 3.14
benchmark dependency site, and `--rigorous` mode.

- [Current-candidate pyperf JSON](data/pyperformance-xlang3-pickle-current-candidate-rigorous-20261004.json)
- [Fixed-Release pyperf JSON](data/pyperformance-xlang3-pickle-fixed-release-rigorous-20261004.json)
- [CPython 3.14.7 full-run reference](data/pyperformance-cpython314-clean-release-full-fast-20261002.json)
- [IR dump of CPython 3.14.7 `pickle.py`](data/pickle-ir-20261004/pickle.ir.txt)
- [Earlier pure-Python Pickler profile and VM-counter notes](pickle-pure-python-current-profile-20261002.md)

Build identities used for the XLang3 comparison:

| Build | `xlang3.exe` SHA-256 | `xlang3_runtime.dll` SHA-256 |
|---|---|---|
| Fixed Release | `B70A6A046513883F808F088C43BC64B7BF7C9672728E74205F3B67AAAADA52DA` | `330BA0B48A931AEF5B927DD0151C0ADF062A9FC5A0C4BC345C51F2BF957DF23F` |
| Current candidate | `57511DD72B472DB10C9A9D2ED3E5AE6F445F3433A04C045416BCCDC8E7567429` | `8A35F064A2C6300BB9E46E0FEC0D4219D596B0D11B29D901ABF43685CEDDD599` |

The all-97 fast-mode run for the current candidate was started separately; its
per-case statuses and failure details will be recorded when it completes.
