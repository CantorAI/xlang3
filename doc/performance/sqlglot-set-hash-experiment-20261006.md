# SQLGlot set-hash dispatch experiment (2026-10-06)

## Result

The direct-call set-hash candidate did not produce a measurable improvement on
`sqlglot_v2_parse`. Keep the pre-experiment Release binaries and reject this
candidate. `pyperf compare_to` hid the result as statistically insignificant.

| Runtime | Mean | Standard deviation |
| --- | ---: | ---: |
| Control | 21.2 ms | 1.4 ms |
| Candidate | 21.5 ms | 1.9 ms |

Both rigorous runs were flagged as unstable. The candidate was numerically
slower, but this difference is noise-level and does not support either a
regression claim or a speedup claim.

## Hypothesis tested

Set membership had two separate hash-dispatch paths. The candidate consolidated
them and, for a Python-function `__hash__`, looked the method up on the class
and passed `self` directly. This was intended to avoid constructing a temporary
bound-method object on each set probe while preserving dynamic class lookup.
The tests confirmed that instance-level `__hash__` shadowing is bypassed and
that changing the class method is observed. However, the end-to-end parse
benchmark showed no gain, so none of that code was retained.

## Reproduction and evidence

Both runs used CPython 3.14.7, the same official `sqlglot_v2_parse` benchmark,
and the established Release executable path:

`D:\CantorAI\xlang3\build-repro\main-verify-20261006\Release\xlang3.exe`

- Control: [`pyperformance-sqlglot-hash-direct-call-control-rigorous-20261006.json`](data/pyperformance-sqlglot-hash-direct-call-control-rigorous-20261006.json)
- Candidate: [`pyperformance-sqlglot-hash-direct-call-candidate-rigorous-20261006.json`](data/pyperformance-sqlglot-hash-direct-call-candidate-rigorous-20261006.json)

The candidate run reported `21.5 ms ± 1.9 ms`; `pyperf compare_to` reported the
benchmark as not significant. The checked-in candidate JSON is retained so this
idea is not mistaken for an established optimization or repeated as a presumed
performance win.

## Validation and disposition

The candidate passed the full fixture suite and the Release interpreter and
runtime-value C++ tests. An optional custom `__hash__` descriptor case failed
in the generic descriptor-resolution path during candidate validation, so it
was excluded from this experiment's fixture. Whether that behavior predates
this candidate was not established; it was not part of the optimization claim.

After measurement, the candidate source changes were removed and the exact
control executable and runtime DLL were restored at the same Release path. The
restored SHA-256 values are `FF66E309BED7F3226F52F59E06842F448992F31805D54685EA755365CCD2D389`
for `xlang3.exe` and
`395BF96C94508A8E9E326D2C79C427489B729AC84CD244BACD4EDC037F331B1E` for
`xlang3_runtime.dll`.
