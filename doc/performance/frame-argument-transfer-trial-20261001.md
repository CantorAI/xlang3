# Python frame argument-transfer trial (2026-10-01)

**Status: rejected; the official benchmarks found no significant improvement.**
The source has been restored to the pre-trial implementation.

## Hypothesis

CPython 3.14's `CALL_PY_EXACT_ARGS` transfers stack references directly into a
new interpreter frame. XLang3's VM already uses indexed call arguments, but
frame initialization retains each argument into the callee's locals. The trial
used move assignment only for small, exact positional calls whose arguments
came from unique caller registers proven dead at that call, and only when a
callee frame slot was already available. Calls with aliases, loop-carried
registers, defaults/keyword binding, frame-vector growth, or more than four
arguments retained the original path.

## Official pyperformance screen

The preserved Release control and candidate ran pyperformance 1.14.0 `--fast`
for the same definitions, dependency site, and Windows shim. All five matching
subtests were statistically insignificant in `pyperf compare_to`:

| Subtest | Control | Candidate | Assessment |
|---|---:|---:|---|
| `deepcopy` | 4.26 ±0.05 ms | 4.25 ±0.08 ms | insignificant |
| `deepcopy_memo` | 528 ±6 µs | 527 ±7 µs | insignificant |
| `deepcopy_reduce` | 41.5 ±0.4 µs | 41.5 ±0.5 µs | insignificant |
| `pickle_pure_python` | 5.66 ±0.10 ms | 5.65 ±0.09 ms | insignificant |
| `unpickle_pure_python` | 3.16 ±0.04 ms | 3.18 ±0.05 ms | insignificant |

Both distributions warn that they do not meet pyperf's 1% stability target.
The changes therefore do not demonstrate a suite-level win and were removed.
The candidate did pass the full **55/55 Release CTest** suite, including
ownership and fixture tests; the fixed-baseline gate was not run for this
rejected candidate.

Raw evidence: [control JSON](data/python-call-arg-transfer-control-fast-20261001.json),
[candidate JSON](data/python-call-arg-transfer-candidate-fast-20261001.json),
[control log](data/python-call-arg-transfer-control-fast-20261001.log),
[candidate log](data/python-call-arg-transfer-candidate-fast-20261001.log),
and [`pyperf compare_to` output](data/python-call-arg-transfer-compare-20261001.txt).

## Release artifact discrepancy found after rollback

After restoring the source, an incremental Release rebuild produced executable
`C432B862C6E1913C6578AC50639C09697283198B60351D7053ED4A943863D57B` and runtime
`3EA73ED069C57642371B9EC239CF6D974904E535DE11C427E543A28368AE969E`. That pair
repeatedly failed `xlang3_cli_net_module_server_client` on `/large` (the client
returned `False` with an empty body). The preserved pre-trial pair
(`84C006FAF5B776BD2715248D16C25F2001BE54B99B9A630E30D025B689BD4866` /
`9CB62863B7782B8758C9AE550AFF62B815893B296C67355A4D3A106F8021FDE3`) passed the
same test. The failing pair is preserved under
[`scratch/performance-baseline/python-call-arg-transfer-rebuilt-no-transfer-failure-20261001`](../../scratch/performance-baseline/python-call-arg-transfer-rebuilt-no-transfer-failure-20261001),
and `build/Release` was restored to the previously validated pair. The earlier
full CTest result for the preserved executable does not validate a fresh
rebuild. A later isolated `build-repro` Release configure and full `ALL_BUILD`
completed from the current source. Its executable and runtime hashes are
`EF20D5E34C5F36F7C7D40FB152311C3449CA505980DAD963CEEBC859578A94A2` and
`BD61627CA60E44E870781C9129F9FFFB5EFECA96A14354568024E90ECB99E153`. This clean pair passed
the same `/large` net client/server test, so the earlier incremental-build
failure did not reproduce in an isolated build. The two artifact pairs differ;
the cause of the old failure is not established.

The clean pair also passed the fixed 11-case Release regression gate against
the preserved control: all cases stayed within the 10% threshold, with
measured candidate/control ratios from about `0.995×` to `1.030×`. The complete
machine-readable result is
[`fresh-release-build-fixed-baseline-20261002.json`](data/fresh-release-build-fixed-baseline-20261002.json).
This is evidence that the clean build has no regression on that local gate;
it is not evidence of a pyperformance improvement.

On clean-build correctness, the focused net integration test passed, and all
53 CTest cases other than the large fixture runner and the Visual Studio
launch-profile test passed. The launch-profile test expects the executable at
the repository's normal `build/Release` path, so it cannot validate an
executable under `build-repro` without changing its configured path. The full
fixture test did not finish during this investigation. In Windows PowerShell
5.1, capturing the `standard_modules.py` fixture's approximately 34 KB stdout
through the runner's `Out-String` pipeline stalled after the fixture
diagnostics, while direct execution and the same capture under PowerShell 7
completed in about 1.5 seconds. Treat the clean build's full CTest result as
incomplete until the fixture-runner capture issue is resolved.
