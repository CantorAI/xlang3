# JSON repeated-key lookup by source view (2026-10-05)

## Result

The native `_json` scanner now probes its per-decoder key memo directly from
the input view for unescaped object keys. Repeated keys can reuse the existing
owned key without first allocating a temporary `Value`. A memo miss creates
the owned XLang string once. Escaped keys retain the original decode-and-memo
path, and JSON's Python wrapper and hook behavior are unchanged.

Official pyperformance 1.14.0 `json_loads` rigorous runs measured **85.0** and
**85.2 µs** for this candidate. The immediately preceding offset-fast-path
checkpoint measured **90.8 µs**, so both new runs are **1.07× faster**. The
preserved pre-change Release binary measured **98.6 µs** in the same session.
The direct call-path diagnostic measured 17.874 µs for `scan_once`, compared
with 19.623 µs for the previous checkpoint. This is a useful incremental
gain, not a solution to the remaining performance gap.

Against the same-day CPython **3.14.7** rigorous result of **17.8 µs**, XLang3
still takes about **4.78× as long**. Pyperf marked the XLang3 rigorous samples
unstable because of large maxima (143–156 µs); the two candidate means agree
closely, but the absolute speedup should be treated as directional until a
quieter repeated run confirms it.

## Correctness and regression checks

- Added fixture coverage for an escaped key followed by its unescaped spelling,
  repeated empty keys, and duplicate-key last-value behavior.
- Full Release CTest passed **55/55** before the fixture extension; the updated
  `xlang3_cli_fixtures` test passed after the extension.
- All 11 fixed Release regression cases passed. The largest candidate/baseline
  ratio was **1.015×** (`subparsers`), below the 1.10× limit.
- The source comments explain why the view lookup is valid (the decoder memo
  contains string keys only) and why allocation is deferred until a miss.

## Run configuration and evidence

Runs used the Python **3.14.7** installation at `C:\Python\Python314`,
pyperformance **1.14.0**, pyperf **2.10.0**, and the shared benchmark
dependencies. XLang3 candidates ran through the repository's Windows
compatibility shim and its Release native `_json` module. The fixed baseline
was `build-repro/Release/xlang3.exe`; the immediately preceding XLang3
checkpoint's raw result is linked below. Candidate executable/runtime SHA-256:
`4CADB0347EC06B6A07F2D85ED2DF97368747AB12E762410BEFAA4AD714147F0D` /
`39FF4D4DC9D7BB71C52FA1AAB49EE97377C04B412CDFBE28B59B658AA3847B6E`.
Fixed baseline executable/runtime SHA-256:
`94F65647D7E3116A81CC7D1E7783D5E951E7A260101667177257F9266502E033` /
`C60087265E4A97FEF73EC4F9CDA28BCDE02A4291FA2E4ED760E4399DB9E9BDE5`.

| Runtime/build | Mean ± stdev | Relative result |
|---|---:|---:|
| CPython 3.14.7 | 17.8 ± 0.4 µs | reference |
| XLang3 repeated-key candidate, run 1 | 85.0 ± 7.8 µs | 4.77× slower than CPython |
| XLang3 repeated-key candidate, run 2 | 85.2 ± 8.2 µs | 4.78× slower than CPython |
| XLang3 fixed Release baseline | 98.6 ± 12.9 µs | 1.16× slower than candidate |
| XLang3 preceding offset-only checkpoint | 90.8 µs | 1.07× slower than candidate |

Raw official results and logs:

- [CPython 3.14.7 rigorous JSON](data/pyperformance-cpython314-jsonloads-key-view-rigorous-20261005.json) and [log](data/pyperformance-cpython314-jsonloads-key-view-rigorous-20261005.log)
- [Candidate run 1](data/pyperformance-xlang3-jsonloads-key-view-candidate-r1-rigorous-20261005.json) and [log](data/pyperformance-xlang3-jsonloads-key-view-candidate-r1-rigorous-20261005.log)
- [Candidate run 2](data/pyperformance-xlang3-jsonloads-key-view-candidate-r2-rigorous-20261005.json) and [log](data/pyperformance-xlang3-jsonloads-key-view-candidate-r2-rigorous-20261005.log)
- [Fixed Release baseline](data/pyperformance-xlang3-jsonloads-key-view-fixed-release-r1-rigorous-20261005.json) and [log](data/pyperformance-xlang3-jsonloads-key-view-fixed-release-r1-rigorous-20261005.log)
- [Offset-only checkpoint result](data/pyperformance-xlang3-jsonloads-offset-candidate-r2-rigorous-20261005.json)
- [CPython comparison table](data/jsonloads-repeated-key-vs-cpython314-20261005.txt)
- [XLang3 comparison table](data/jsonloads-repeated-key-vs-xlang3-control-20261005.txt)
- [Call-path diagnostic](data/jsonloads-repeated-key-callpath-candidate-20261005.csv)
- [11-case fixed Release gate](data/jsonloads-repeated-key-fixed-baseline-gate-20261005.json)

The diagnostic is a local call-path measurement, not an official pyperformance
score. Its row-by-row results can be reproduced with
`benchmarks/diagnostics/json_loads_callpath_split.py 30 9` under the candidate
and CPython 3.14.7 interpreters.
