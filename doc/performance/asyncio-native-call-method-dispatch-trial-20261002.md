# Asyncio native method call dispatch trial (2026-10-02)

This change reduces repeated internal asyncio method-call overhead by bypassing
temporary bound-method creation for eligible method calls. In
[`asyncio_module.cpp`](../../src/runtime/modules/system/asyncio_module.cpp),
the helper resolves an ordinary, unshadowed Python function descriptor or
native bound descriptor and invokes it through the existing callable path with
`self` prepended. Instance attributes, slots, custom `__getattribute__`,
native attribute hooks, and other descriptor cases retain normal lookup. The
code comment records why these guards are required and which dispatch work the
fast path removes.

The regression fixture
[`asyncio_native_call_method_dispatch.py`](../../tests/fixtures/core/asyncio_native_call_method_dispatch.py)
checks subclass overrides, instance shadowing, and class replacement after
loop construction. The fixture passed under XLang3 and CPython 3.13 with
matching output.

## Measurement

The target was `async_tree_eager`, measured with pyperf's `debug` mode, which
produces one timed score per process. Three separate control/candidate runs
were made in alternating order against source-matched Release executables.
The aggregate `pyperf compare_to` result is **1.05× faster** for the candidate.

| Build | Three scores | Median |
|---|---|---:|
| Control | 3.440, 3.417, 3.360 s | 3.417 s |
| Candidate | 3.255, 3.215, 3.267 s | 3.255 s |

All three paired observations favor the candidate. This is a small, targeted
gain; it does not close the large asyncio gap. These scores use the available
Python 3.13 standard library and are an A/B comparison between XLang3 builds.
They must not be compared directly with the CPython 3.14.7 score in the full
suite report, which uses a different standard library and benchmark run.

The complete fixed Release regression gate passed against the preserved
`baseline-0336992` build: all 11 cases passed with 21 order-balanced paired
samples and the default 10% threshold. The gate compares XLang3 builds, not
CPython. Candidate/baseline time ratios were: `local_slots` 0.375×,
`scalar_arithmetic` 0.985×, `range_for` 1.021×, `function_calls` 0.985×,
`class_construct` 0.993×, `list_append` 1.014×, `property_access` 1.001×,
`deepcopy_memo` 0.565×, `json_dumps` 0.824×, `gc_traversal` 1.013×, and
`subparsers` 0.875×. No case exceeded the regression threshold.

## Evidence

- Control scores: [run 1](data/asyncio-native-call-method-control-debug-r1.json),
  [run 2](data/asyncio-native-call-method-control-debug-r2.json),
  [run 3](data/asyncio-native-call-method-control-debug-r3.json), and
  [aggregate](data/asyncio-native-call-method-control-debug-aggregate.json).
- Candidate scores: [run 1](data/asyncio-native-call-method-candidate-debug-r1.json),
  [run 2](data/asyncio-native-call-method-candidate-debug-r2.json),
  [run 3](data/asyncio-native-call-method-candidate-debug-r3.json), and
  [aggregate](data/asyncio-native-call-method-candidate-debug-aggregate.json).
- [Fixed-baseline Release gate](data/asyncio-native-call-method-fixed-baseline-gate-with-compat.json).

## Python 3.14.7 validation (2026-10-02)

The targeted pyperformance test was rerun on both source-matched Release
executables with the Python 3.14.7 standard library, pyperformance 1.14.0,
and the same installed benchmark dependencies. The control measured 3.20 s and
the candidate 3.06 s for `async_tree_eager` (**1.05× faster**). Both runs
reported pyperf's low-sample stability warning, so treat the improvement as
small and provisional; it does not account for the much larger remaining
asyncio gap.

- [Control Python 3.14.7 pyperf JSON](data/asyncio-native-call-method-control-python314-stdlib-fast-20261002.json)
- [Candidate Python 3.14.7 pyperf JSON](data/asyncio-native-call-method-candidate-python314-stdlib-fast-20261002.json)
- [Current 3.14.7 fixed Release regression gate](data/release-regression-python314-stdlib-async-dispatch-20261002.json): all 11 cases passed. The ratios are in candidate/baseline time, so below 1.0 is faster: `local_slots` 0.383×, `scalar_arithmetic` 0.988×, `range_for` 1.001×, `function_calls` 0.982×, `class_construct` 0.998×, `list_append` 0.991×, `property_access` 0.982×, `deepcopy_memo` 0.524×, `json_dumps` 0.809×, `gc_traversal` 0.868×, and `subparsers` 0.795×.

The full Python 3.14.7 pyperformance run covered all 97 definitions: 44
completed and 53 failed or timed out; 48 subtests could be compared with
CPython, with four XLang3 wins and a geometric-mean speed ratio of 0.159×
(about 6.27× slower overall). The biggest gaps include `telco`,
`async_tree_eager`, and pure-Python pickle. See the
[full comparison](pyperformance-xlang3-current-python314-stdlib-full-fast-20261002.md)
for the full status list, chart, and limitations. The CPython performance
goal remains open.

### Corrected full run (2026-10-03)

The first run's `sitecustomize` imported pyperf in pyperf's own
`_process_time.py` timer helper. That helper deliberately exits if `pyperf` is
already loaded, so this was a benchmark-harness defect rather than an XLang3
runtime failure. The shim now skips its pyperf hooks in that one helper
process. The Python 3.14.7 host then completed all 97 definitions: 47 finished
and 50 failed or timed out. It produced 51 matched subtests, five XLang3 wins,
and a CPython/XLang3 geometric-mean ratio of 0.16747× (about 5.97× slower).
The additional completed definitions were `2to3`, `python_startup`, and
`python_startup_no_site`; no runtime speedup is claimed from this harness-only
correction. The fast-mode full report and all-97 status table are available
[here](pyperformance-xlang3-current-python314-fixed-shim-full-fast-20261003.md).

The highest remaining slowdowns are `telco` (0.023×), `async_tree_eager`
(0.028×), `pickle_pure_python` (0.053×), and `subparsers` (0.053×). Several
async-tree, bytes-processing, and formatting benchmarks still hit their caps;
many dependency-heavy benchmark workers die. The performance goal remains
open, and the next runtime work should target these measured gaps rather than
treating the corrected harness count as a performance gain.
