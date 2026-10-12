# JSON scanner ASCII offset fast path (2026-10-05)

## Result

The native XLang3 `_json.Scanner` now avoids two Unicode-index scans on the
default ASCII input path used by `json.loads()`: validating the normal start
index of zero, and converting the parser's byte end offset to a Python
character index. While parsing strings, the scanner records whether it saw
any raw non-ASCII bytes. ASCII input can use its byte offset directly; Unicode
input and nonzero start-index validation retain the general code-point path.

This improves XLang3's official pyperformance 1.14.0 `json_loads` result by
about 9–11% in two order-interleaved rigorous pairs. It does not close the
remaining gap to CPython 3.14.7: the candidate is still about **4.7× slower**
on this case. The `_json` accelerator is native in CPython too; `json.py`
remains Python and its public API/extension points are unchanged.

## Measurements

All runs used CPython **3.14.7** as the benchmark harness, pyperformance
**1.14.0**, the same dependency site, and the official `json_loads` benchmark
in `--rigorous` mode. Order was control, candidate, control, candidate.
Pyperf reported high maxima and marked each run unstable; the two separate
control/candidate pairs nevertheless show the same direction and similar
magnitude.

| Pair | Fixed Release control | Candidate | Candidate speedup |
| --- | ---: | ---: | ---: |
| 1 | 99.2 µs ± 11.1 µs | 89.6 µs ± 8.8 µs | 1.11× |
| 2 | 98.7 µs ± 7.9 µs | 90.8 µs ± 14.5 µs | 1.09× |

The saved CPython 3.14.7 full-suite reference is 19.33 µs, so the candidate
remains around 4.67× slower. A same-process call-path diagnostic using
pyperformance's exact three payloads measured:

| Path | CPython 3.14.7 | XLang3 control | XLang3 candidate |
| --- | ---: | ---: | ---: |
| `json.loads` | 5.75 µs/load | 31.49 µs/load | 28.61 µs/load |
| `JSONDecoder.decode` | 5.73 µs/load | 25.43 µs/load | 22.74 µs/load |
| `JSONDecoder.raw_decode` | 5.26 µs/load | 22.32 µs/load | 19.84 µs/load |
| direct `scan_once` | 5.24 µs/load | 21.64 µs/load | 19.62 µs/load |

The path split shows that most remaining cost is in native scanning and
constructing XLang `Value` containers; the public Python wrapper adds a
smaller portion. This is evidence for the next `_json`/runtime investigation,
not a claim that wrappers are free.

## Correctness and release gate

- Added `raw_decode` coverage for an ASCII document at a nonzero start index,
  plus raw-Unicode strings at zero and nonzero indices. Existing JSON tests
  continue to cover escaped Unicode, invalid input, custom hooks, and owned
  input lifetime.
- Full Release CTest passed **55/55**.
- The complete 11-case fixed Release regression gate passed. Its largest
  candidate/control time ratio was `scalar_arithmetic` at **1.035×**, within
  the 1.10 limit. The JSON-load optimization did not regress `json_dumps`
  (1.008×).
- The fixed baseline remained at `build-repro/Release`; no baseline artifact
  was modified.

## Reproduction evidence

- [Rigorous fixed-control run 1](data/pyperformance-xlang3-jsonloads-offset-control-rigorous-20261005.json)
- [Rigorous candidate run 1](data/pyperformance-xlang3-jsonloads-offset-candidate-r1-rigorous-20261005.json)
- [Rigorous fixed-control run 2](data/pyperformance-xlang3-jsonloads-offset-control-r2-rigorous-20261005.json)
- [Rigorous candidate run 2](data/pyperformance-xlang3-jsonloads-offset-candidate-r2-rigorous-20261005.json)
- [pyperf comparison table](data/jsonloads-offset-rigorous-compare-20261005.txt)
- [Fixed Release regression gate](data/jsonloads-offset-fixed-baseline-gate-20261005.json)
- [Call-path diagnostic script](../../benchmarks/diagnostics/json_loads_callpath_split.py), with [CPython data](data/jsonloads-offset-callpath-cpython314-20261005.csv), [control data](data/jsonloads-offset-callpath-control-20261005.csv), and [candidate data](data/jsonloads-offset-callpath-candidate-20261005.csv).
