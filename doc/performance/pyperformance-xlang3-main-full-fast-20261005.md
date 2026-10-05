# XLang3 `main` vs CPython 3.14.7: full pyperformance run

This run measures the current `main` Release executable against the saved CPython 3.14.7 Release reference. It attempts all 97 pyperformance definitions. The run completed 46 definitions and recorded 51 failures or timeouts. Among 50 matched subtests, XLang3 was faster in 5 and slower in 45; the geometric mean of CPython time divided by XLang3 time is **0.17324×**. Ratios above 1× favor XLang3.

![Horizontal log-scale chart; bars right of 1× favor XLang3](pyperformance-xlang3-main-full-fast-20261005.svg)

## Run configuration

- Benchmark suite: pyperformance 1.14.0, `--fast` mode, all 97 definitions.
- Reference runtime: CPython 3.14.7 at `C:\Python\Python314`, from the clean Release reference run dated 2026-10-02.
- XLang3 revision: `ff7d9bfcf5051544172a1045d3897a8e99426085` (`main`).
- XLang3 Release executable SHA-256: `0D86A3A269776639F4CFA7CE9F6D34BF02239CC43A6E0E877C128A07B3882A55`.
- XLang3 runtime DLL SHA-256: `2ADC956634C5F3793A6B1C162368315F47B7E63AB8D58082A94A58AFF39BDAEC`.
- Standard library: Python 3.14 standard library. The Windows pyperf compatibility shim and shared dependency site-packages were used; no Python 3.13 overlay was used.
- Per-definition timeout: 120 seconds; `async_tree*` definitions were capped at 30 seconds except `async_tree` and `async_tree_eager`, each capped at 300 seconds.
- The exact run log includes command output and every attempted definition. Fast-mode results are directional: pyperf warns that many measurements do not have enough samples for stable estimates.

## Largest measured slowdowns

| Subtest | CPython 3.14.7 | XLang3 | CPython / XLang3 |
|---|---:|---:|---:|
| `async_tree_none` | 227.4 ms | 5.137 s | 0.0443× |
| `pickle_pure_python` | 273.5 µs | 5.188 ms | 0.0527× |
| `subparsers` | 8.151 ms | 149.9 ms | 0.0544× |
| `async_tree_eager` | 86.62 ms | 1.402 s | 0.0618× |
| `logging_silent` | 70.03 ns | 1.085 µs | 0.0646× |
| `deepcopy` | 217.8 µs | 2.811 ms | 0.0775× |
| `deltablue` | 3.004 ms | 37.86 ms | 0.0793× |
| `comprehensions` | 14.22 µs | 173.8 µs | 0.0818× |
| `richards_super` | 41.37 ms | 475.8 ms | 0.0869× |
| `telco` | 5.755 ms | 40.90 ms | 0.1407× |
| `json_dumps` | 8.063 ms | 35.45 ms | 0.2275× |
| `json_loads` | 19.33 µs | 83.69 µs | 0.2310× |

## Measured wins

| Subtest | CPython 3.14.7 | XLang3 | CPython / XLang3 |
|---|---:|---:|---:|
| `gc_traversal` | 2.332 ms | 1.235 ms | 1.889× |
| `pickle_list` | 4.625 µs | 3.932 µs | 1.176× |
| `python_startup_no_site` | 20.68 ms | 17.66 ms | 1.171× |
| `fannkuch` | 324.0 ms | 284.5 ms | 1.139× |
| `pickle_dict` | 26.41 µs | 24.14 µs | 1.094× |

The `gc_traversal` result measures the benchmark's traversal workload and does not imply that XLang3 implements cyclic garbage collection. `gc_collect` fails because the benchmark expects collection of reference cycles, which XLang3 does not provide.

## Full-suite outcomes

The full-definition status CSV records CPython and XLang3 status, all available subtest timings, failure details, and cases without a matched score. The 51 XLang3 failures divide into 19 timeouts and 32 worker failures. Important distinctions include:

- Timeouts: all 14 secondary `async_tree*` variants, `asyncio_tcp`, `asyncio_tcp_ssl`, `base64`, `bpe_tokeniser`, and `pprint`.
- Missing optional packages: examples include `websockets`, `chameleon`, `coverage`, `pyaes`, `dask`, `django`, `docutils`, `dulwich`, `httpx`, `genshi`, `html5lib`, `mako`, `networkx`, `sympy`, `tomli`, `tornado`, and `xdsl`.
- Runtime or compatibility failures: `gc_collect` asserts because cycles are not collected; `mdp` asserts in its workload; SciMark fails in its FFT array slice path; `sqlite_synth` expects `Connection.create_aggregate`; `concurrent_imap` fails during Windows multiprocessing worker startup; `xml_etree` also fails in its worker.

Worker failures are not performance scores. Optional-package failures are environment limitations, while runtime assertion and missing API failures indicate compatibility work. The raw log should be consulted for complete tracebacks and exact failure messages.

## Next profiling targets

1. **Async task stepping** is the largest measured gap and the secondary variants frequently exceed even the 30-second cap. The two completed workloads remain 16–23× slower. A small `async_tree_call_profile.py` probe (levels=3, branches=3, 5 fresh event loops) counted 5,742 Python asyncio call events in CPython and 5,792 in XLang3. That near-equal count does not explain the timing gap and points toward per-operation VM cost or native task/coroutine stepping. Source inspection shows `asyncio_task.cpp:step_impl` calls `generator_send`, and `generator_send` constructs an `Interpreter` before `resume_generator`; CPython's Task path advances its native coroutine with `PyIter_Send`. This is a concrete profiling target, not yet a proven single cause. Earlier call-dispatch and event-polling trials were rejected as neutral or regressive.
2. **Pure-Python execution overhead** is visible in pickle, subparsers, deepcopy, logging, and comprehensions. Respect the project rule: do not replace CPython pure-Python standard-library implementations with C++; optimize XLang3's interpreter and call/runtime machinery instead.
3. **JSON native module** remains 4.4× slower on `json_dumps` and 4.3× slower on `json_loads`. CPython uses `_json`; inspect XLang3's own `_json` registration and profile its existing native path before changing it.
4. **Telco** improved from 185.9ms in the prior full run to 40.9ms after the Decimal signal-identity change, but remains 7.1× slower. Re-profile current `telco` before considering a separate Decimal optimization.

## Evidence files

- [Horizontal speed-ratio chart](pyperformance-xlang3-main-full-fast-20261005.svg)
- [All 97 definitions: CPython and XLang3 status](data/pyperformance-xlang3-main-full-fast-20261005-all-97-status.csv)
- [Every matched and unmatched subtest](data/pyperformance-xlang3-main-full-fast-20261005-subtests.csv)
- [Raw XLang3 pyperf JSON](data/pyperformance-xlang3-main-full-fast-20261005.json)
- [Full XLang3 runner log](data/pyperformance-xlang3-main-full-fast-20261005.log)
- [CPython 3.14.7 pyperf JSON](data/pyperformance-cpython314-clean-release-full-fast-20261002.json)
- [CPython 3.14.7 runner log](data/pyperformance-cpython314-clean-release-full-fast-20261002.log)
