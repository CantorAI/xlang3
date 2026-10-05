# `Context.run` map-transfer trial (2026-10-01)

**Status: correctness and fixed-baseline gate pass; targeted A/B is inconclusive.**
The change avoids copying the active context map and takes a direct callable path
when `Context.run` has no keyword arguments. The latter is the common callback
shape used by asyncio's `Handle`/Task stepping path. Neither change has yet
shown a repeatable material pyperformance gain.

## Change and semantic check

`src/runtime/modules/system/contextvars_module.cpp` previously copied the
thread-local binding map into a local map, copied a saved Context map into the
thread-local map, then copied both maps again on return. It now swaps maps into
place, saves the callback's updated bindings by swapping back, and restores the
caller map. This makes the context transfer independent of the number of
bindings. Calls with zero keyword arguments also bypass the generic keyword
call adapter. The comments in the implementation describe the CPython
current-Context pointer-swap analogue and the per-Task hot path.

`tests/fixtures/core/context_run_keywords.py` now covers nested runs with two
different Context objects. The fixture output was verified on both XLang3 and
CPython 3.14. Release CTest passes **55/55**.

## Official benchmark evidence

All samples used pyperformance 1.14.0's `async_tree` definition, which reports
the `async_tree_none` subtest, the same dependency site, the Windows
compatibility shim, and separate `--debug-single-value` invocations.

| Binary / variant | `async_tree_none` time |
|---|---:|
| XLang3 before map swap (one completed control) | 5.534 s |
| XLang3 map-swap candidate | 5.430 s, 5.272 s |
| XLang3 map-swap + positional-call candidate | 5.430 s, 5.272 s, 5.360 s |
| CPython 3.14.7 saved reference | 0.233 s |

The map-swap/positional-call candidate median is **5.360 s**, about **23.0×
slower** than the saved CPython reference. Candidate samples range from 5.272 s
to 5.430 s. Only one comparable copy-path control completed; two subsequent
control invocations stalled in pyperf workers and reached the 120-second
case cap without producing a score. The roughly 3% difference from the lone
control is therefore not a confirmed optimization. Keep this trial out of any
claim that the asyncio gap is solved.

Completed candidate result files:

- [`context-copy control`](data/pyperformance-xlang3-async-tree-context-copy-control-debug-20261001.json)
- [`map-swap candidate`](data/pyperformance-xlang3-async-tree-context-swap-candidate-debug-20261001.json)
- [`map-swap repeat`](data/pyperformance-xlang3-async-tree-context-swap-candidate-r2-20261001.json)
- [`positional-call candidate`](data/pyperformance-xlang3-async-tree-context-direct-call-candidate-debug-20261001.json)
- [`positional-call repeat 1`](data/pyperformance-xlang3-async-tree-context-direct-call-candidate-r2-20261001.json)
- [`positional-call repeat 2`](data/pyperformance-xlang3-async-tree-context-direct-call-candidate-r3-20261001.json)

The timed-out control commands produced no pyperf JSON result. Their observed
terminal outcome was `Full-case timeout: async_tree exceeded 120 seconds`.
Treat those as failed runs, never as timing samples.

## Release validation

- Fixed Release gate: **pass**, 11 cases, 21 paired samples, 10% threshold.
- Gate report: [`contextvars-context-run-fixed-baseline-20261001.json`](data/contextvars-context-run-fixed-baseline-20261001.json)
- Gate ratios (candidate/baseline): local_slots 0.0789×, scalar_arithmetic
  0.0222×, range_for 0.0184×, function_calls 0.0066×, class_construct 0.0451×,
  list_append 0.0198×, property_access 0.0041×, deepcopy_memo 0.5003×,
  json_dumps 0.0292×, gc_traversal 0.1797×, subparsers 0.6688×.
- Release executable SHA-256: `3afb3e841a8ba45ae5cb0df05a21fa6390c786709a5dab6b1a11cfaa0906483b`.
- Release runtime DLL SHA-256: `94cefb57f705b9e2a3c53026c7f5e19857d7aa8b750f52fde934307255ef6b72`.
- Fixed baseline executable SHA-256: `3f64142d062d470fba5d8569174b26381a1b92e3a0eabff65a4df09e31348373`.

## Full-suite status

A post-change pyperformance 1.14.0 full-suite run completed with the shared
CPython 3.14 dependency site. The command selected all **97** definitions in
`fast` mode, capped each full case at 120 seconds, and saved the
[log](data/pyperformance-xlang3-current-asyncio-context-fast-20261001.log) and
[JSON result](data/pyperformance-xlang3-current-asyncio-context-fast-20261001.json).
The run attempted all definitions: **46 completed and 51 failed or timed out**.
Of the 16 async-tree definitions, `async_tree_eager` completed in 1.90 s and the
other 15 reached the 120-second cap. `asyncio_tcp` and `asyncio_tcp_ssl` also
timed out; `asyncio_websockets` completed in 502 ms. `base64` and
`bpe_tokeniser` timed out. `chameleon` failed with `TypeError: str.split
expected 0 to 2 arguments` inside the pure-Python Chameleon parser; that
compatibility failure is recorded without proposing a C++ replacement for a
pure-Python standard-library module. `concurrent_imap`, `coverage`, `dask`,
`django_template`, `docutils`, `dulwich_log`, `fastapi`, `gc_collect`, and
`genshi` failed.
The run also completed `2to3`, `argparse`, `argparse_subparsers`,
`async_generators`, `async_tree_eager`, `chaos`, `comprehensions`, `coroutines`,
`crypto_pyaes`, `deepcopy`, `deltablue`, `fannkuch`, `float`, `gc_traversal`,
and `generators`. A complete CPython 3.14 comparison, horizontal ratio chart,
and all-97 status table are recorded in
[`pyperformance-xlang3-vs-cpython314-contextvars-asyncio-fast-20261001.md`](pyperformance-xlang3-vs-cpython314-contextvars-asyncio-fast-20261001.md).
