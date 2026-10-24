# Exact dict-items pair-unpack trial (2026-10-06)

## Result

The proposed `dict.items()` tuple-elision path did not improve the official
pyperformance `deepcopy` family. The source change and its supporting loop
liveness adjustment were removed. The candidate with both changes measured
`deepcopy` at 2.51 ms versus 2.45 ms for the source-matched control; pyperf
reported 1.02× slower. `deepcopy_memo` was also 1.02× slower, and
`deepcopy_reduce` showed no significant change. This is a rejected experiment,
not a performance claim.

## Why this looked promising

The installed Python 3.14.7 `copy.py` implements `_deepcopy_dict` as a Python
loop over `x.items()`, immediately unpacks each pair, recursively copies the
key and value, and stores them into the result. XLang3's IR has the same
three-instruction sequence:

```text
IterNext -> UnpackSequence (exact pair) -> StoreLocalPair
```

The prototype guarded that shape, exact `DictItemsView` iterator, disabled
debug/monitoring and opcode counters, and dead temporary registers. It copied
both dict entry values before replacing locals, because releasing an old local
can run a finalizer that mutates the source dict. Other iterator kinds and all
observable paths retained the original behavior. The pure-Python `copy.py`
was not changed.

## What the first flat comparison taught us

The first prototype's `IterNext` and unpack results were marked loop-carried by
`XlangVMFrame::compute_register_last_use`, even though these instructions
overwrite their temporary registers each pass. Its safety checks therefore
disabled tuple elision in `_deepcopy_dict`. That first A/B pair was flat at
2.47 ms for `deepcopy`; it did not test the intended fast path.

I corrected the temporary classification for `IterNext` and
`UnpackSequence`, verified the full fixture runner and interpreter tests, and
rebuilt the candidate. The resulting liveness-only candidate was 1.01× slower
than the separately saved control in pyperf's fast comparison. Adding tuple
elision on top made the result 1.02× slower. There was no case-level gain to
justify the additional liveness and dispatch logic.

## Measurements and builds

All runs used pyperformance 1.14.0 with CPython **3.14.7** at
`C:\Python\Python314`, its unchanged installed standard library and shared
benchmark dependencies. The same official `bm_deepcopy` body ran in both
XLang3 executables. Fast-mode stability warnings mean these samples are a
screen, not a replacement for a rigorous result or the full-suite comparison.

| XLang3 build | `deepcopy` | `deepcopy_reduce` | `deepcopy_memo` | Decision |
|---|---:|---:|---:|---|
| Source-matched control | 2.45 ms | 27.4 us | 267 us | Reference |
| Pair-elision prototype before liveness correction | 2.47 ms | 27.5 us | 266 us | No significant change; guard made it a no-op |
| Liveness-only candidate | 2.49 ms | 27.5 us | 267 us | 1.01× slower on `deepcopy`; no gain |
| Pair elision plus liveness correction | 2.51 ms | 27.5 us | 271 us | 1.02× slower on `deepcopy` and `deepcopy_memo` |

The source-matched control executable/runtime SHA-256 values are
`10B09AC8F1EF459339A271DF77C858EA6C71021905C426E1001F0DE7377A8513` and
`2E27F1C65D250BE9D3B8331185F7092E26EE9074A9D74AB3CEC20682E24C3E79`.
The combined candidate executable/runtime hashes are
`4B7E97B363627AECFBCA320BCF3C3A7756073A457C9CE279AC5AD29C328D47F8` and
`B13367E930FE8B2C41E82ECA0455D7BBA05195C7D58DE2AC632CF97012CF44A1`.
For context, the saved CPython 3.14.7 full-run `deepcopy` score is 218 us;
the combined candidate at 2.51 ms is 11.53× slower. This experiment does not
close the CPython gap.

The candidate passed `xlang3_interpreter_tests.exe` and the full
`tests/run_fixtures.py` suite with Python 3.14.7. No fixed Release baseline
files were changed. Since the candidate failed its targeted performance
screen, I did not run the fixed-baseline performance gate or the full
pyperformance suite for it.

## Raw data

- Initial pair-elision control and candidate: [control](data/deepcopy-dict-pair-control-fast-20261006.json), [candidate](data/deepcopy-dict-pair-candidate-fast-20261006.json), [repeat control](data/deepcopy-dict-pair-control2-fast-20261006.json), [repeat candidate](data/deepcopy-dict-pair-candidate2-fast-20261006.json).
- Liveness-only and combined pair-elision comparison: [liveness-only](data/deepcopy-loop-liveness-candidate-fast-20261006.json), [combined control](data/deepcopy-pair-liveness-control-fast-20261006.json), [combined candidate](data/deepcopy-pair-liveness-candidate-fast-20261006.json).
- The original `deepcopy` diagnosis and current full-suite CPython comparison remain in [deepcopy-call-and-dispatch-diagnosis-20261006.md](deepcopy-call-and-dispatch-diagnosis-20261006.md) and [pyperformance-xlang3-main-after-exception-guard-full-fast-20261006.md](pyperformance-xlang3-main-after-exception-guard-full-fast-20261006.md).

The next optimization should target a separately measured cost in the shared
call/frame or object-lifetime paths. Do not retry tuple allocation removal or
the same loop-liveness adjustment as a `deepcopy` speed fix without new
evidence.
