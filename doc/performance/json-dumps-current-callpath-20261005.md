# `json.dumps` current call-path diagnosis (2026-10-05)

## Finding

The built-in XLang3 `_json` encoder is not the source of the small-payload
`json_dumps` slowdown. The large-payload case already beats CPython 3.14.7,
while XLang3's Python `json.py` wrapper path costs several additional
microseconds per small call. The next optimization target is generic VM Python
call/frame overhead; `json.encoder` remains Python and keeps its existing
semantics.

This is a call-path diagnostic, not an official pyperformance score. The
authoritative full-suite result remains
[`pyperformance-xlang3-current-release-all-97-vs-cpython314-fast-20261005.md`](pyperformance-xlang3-current-release-all-97-vs-cpython314-fast-20261005.md).
That run measured the matched `json_dumps` subtest at 34.084 ms for XLang3 and
8.063 ms for CPython 3.14.7 (0.237× CPython/XLang3).

## Measurement

The same `benchmarks/diagnostics/json_dumps_callpath_split.py` script ran on
CPython 3.14.7 and the fixed XLang3 Release executable. Each row is the median
of seven timed passes after one warm-up pass, using the script's payload and
call counts. Each path steps farther down the public wrapper stack until it
calls `_json.make_encoder` directly.

| Payload | Calls | Path | CPython 3.14.7 (µs/call) | XLang3 (µs/call) | CPython / XLang3 |
|---|---:|---|---:|---:|---:|
| Empty | 2,000 | `json.dumps` | 0.855 | 8.489 | 0.101× |
| Empty | 2,000 | `JSONEncoder.encode` | 0.722 | 7.340 | 0.098× |
| Empty | 2,000 | `iterencode + join` | 0.618 | 4.616 | 0.134× |
| Empty | 2,000 | direct `_json` + join | 0.161 | 0.901 | 0.179× |
| Simple | 1,000 | `json.dumps` | 1.444 | 8.992 | 0.161× |
| Simple | 1,000 | `JSONEncoder.encode` | 1.307 | 7.747 | 0.169× |
| Simple | 1,000 | `iterencode + join` | 1.186 | 4.883 | 0.243× |
| Simple | 1,000 | direct `_json` + join | 0.649 | 1.252 | 0.519× |
| Nested | 1,000 | `json.dumps` | 2.728 | 9.677 | 0.282× |
| Nested | 1,000 | `JSONEncoder.encode` | 2.585 | 8.508 | 0.304× |
| Nested | 1,000 | `iterencode + join` | 2.451 | 5.740 | 0.427× |
| Nested | 1,000 | direct `_json` + join | 1.830 | 1.941 | 0.943× |
| Huge | 1 | `json.dumps` | 1,503.3 | 701.7 | 2.142× |
| Huge | 1 | `JSONEncoder.encode` | 1,406.6 | 707.6 | 1.988× |
| Huge | 1 | `iterencode + join` | 1,375.9 | 700.4 | 1.965× |
| Huge | 1 | direct `_json` + join | 1,395.2 | 667.0 | 2.092× |

Values above 1× favor XLang3. The large case serializes a 1,000-element list;
the small cases emphasize repeated Python calls and wrapper work. Differences
between adjacent rows help localize wrapper cost but are not independent
microbenchmarks of individual Python operations.

## Interpretation and next step

For empty values, the direct native path takes 0.901 µs in XLang3 versus 0.161
µs in CPython, but `json.dumps` takes 8.489 µs versus 0.855 µs. For the large
value, the native path is 2.092× faster in XLang3 and the public method is
2.142× faster. This rules against replacing or merely re-registering `_json`
as the fix for the full pyperformance slowdown.

The profile evidence and the step-down timings instead point to repeated
Python wrapper/function calls and frame setup. Optimize shared VM call/frame
paths, preserve Python override/traceback/monitoring behavior with guards and
fallbacks, then require both the official `json_dumps` pyperformance result
and the fixed Release regression gate before accepting an engine change.

## Reproduction artifacts

- [CPython 3.14.7 call-path CSV](data/json-dumps-callpath-split-cpython314-20261005.csv)
- [XLang3 Release call-path CSV](data/json-dumps-callpath-split-xlang3-20261005.csv)
- [Diagnostic script](../../benchmarks/diagnostics/json_dumps_callpath_split.py)
- XLang3 executable SHA-256: `94F65647D7E3116A81CC7D1E7783D5E951E7A260101667177257F9266502E033`.
- CPython version: `3.14.7` (`C:\Python\Python314`).
