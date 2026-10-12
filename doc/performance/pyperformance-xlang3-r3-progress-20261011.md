# Pyperformance r3 run record — 2026-10-11

## Final status

The run is complete: all **97** pyperformance 1.14.0 definitions were attempted.
The output JSON contains **70** matched subtest measurements; **61** definitions
completed and **36** failed or timed out (10 timeouts and 26 worker deaths).
Against the CPython 3.14.7 reference, the geometric mean is **0.14068× as fast**
for XLang3, or **7.11× longer**. Fast-mode timings are directional and carry
stability warnings. See the [completed comparison and chart](pyperformance-xlang3-fixed-release-r3-vs-cpython3147-full-fast-20261011.md),
the [all-97 status CSV](data/pyperformance-xlang3-fixed-release-r3-vs-cpython3147-full-fast-20261011-all-97-status.csv),
and the [full subtest CSV](data/pyperformance-xlang3-fixed-release-r3-vs-cpython3147-full-fast-20261011-subtests.csv).

The table below is a preserved **case-96 progress snapshot**, not the final
results. Full-run console stdout was not saved; the reconstructed status record
is explicitly labeled as reconstructed and is not raw runner output.

The official run used pyperformance 1.14.0 in
`--fast` mode, CPython 3.14.7 as the harness/dependency environment, the shared
`C:\Python\Python314\Lib\site-packages`, and the fixed Release executable at
`build-repro/main-verify-20261006/Release/xlang3.exe` with a 300-second cap per
definition. The output is being written to
[`pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r3.pyperf.json`](data/pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r3.pyperf.json).

At case 96 of 97, the following results had been emitted; case 97, `xml_etree`,
is running:

| Case | Result |
| --- | ---: |
| `2to3` | 2.76 s ± 0.28 s |
| `argparse` / `many_optionals` | 8.96 ms ± 0.65 ms |
| `argparse_subparsers` / `subparsers` | 137 ms ± 3 ms |
| `async_generators` | 2.46 s ± 0.05 s |
| `async_tree_none` | 3.89 s ± 0.28 s |
| `async_tree_cpu_io_mixed` | 5.00 s ± 0.62 s |
| `async_tree_cpu_io_mixed_tg` | 4.80 s ± 0.09 s |
| `async_tree_eager` | 1.38 s ± 0.07 s |
| `async_tree_eager_cpu_io_mixed` | 2.61 s ± 0.14 s |
| `async_tree_eager_cpu_io_mixed_tg` | 4.51 s ± 0.08 s |
| `async_tree_eager_memoization` | 3.27 s ± 0.10 s |
| `async_tree_eager_memoization_tg` | 5.46 s ± 0.36 s |
| `async_tree_eager_tg` | 3.51 s ± 0.14 s |
| `async_tree_eager_io` | 300 s timeout |
| `async_tree_eager_io_tg` | 300 s timeout |
| `async_tree_io` | 300 s timeout |
| `async_tree_io_tg` | 300 s timeout |
| `async_tree_memoization` | 5.52 s ± 0.97 s |
| `async_tree_memoization_tg` | 5.29 s ± 0.38 s |
| `async_tree_none_tg` | 3.76 s ± 0.20 s |
| `asyncio_tcp` | 4.03 s ± 0.29 s |
| `asyncio_tcp_ssl` | 300 s timeout |
| `asyncio_websockets` | Benchmark died: `websockets` dependency missing |
| `base64` | 300 s timeout; partial subtests saved |
| `base64` / `base64_small` | 2.69 ms ± 0.12 ms |
| `base64` / `base64_large` | 14.5 ms ± 0.2 ms |
| `base64` / `urlsafe_base64_small` | 5.42 ms ± 0.07 ms |
| `base64` / `base32_small` | 40.8 ms ± 0.6 ms |
| `base64` / `base32_large` | 1.85 s ± 0.06 s |
| `base64` / `base16_small` | 3.26 ms ± 0.04 ms |
| `base64` / `base16_large` | 39.6 ms ± 0.7 ms |
| `base64` / `ascii85_small` | 96.3 ms ± 0.9 ms |
| `bpe_tokeniser` | 300 s timeout |
| `chameleon` | Benchmark died: `chameleon` dependency missing |
| `chaos` | 479 ms ± 39 ms |
| `comprehensions` | 175 µs ± 5 µs |
| `concurrent_imap` / `bench_mp_pool` | 338 ms ± 8 ms |
| `concurrent_imap` / `bench_thread_pool` | 9.73 ms ± 0.46 ms |
| `coroutines` | 145 ms ± 2 ms |
| `coverage` | Benchmark died: `coverage` dependency missing |
| `crypto_pyaes` | Benchmark died: `pyaes` dependency missing |
| `fastapi` | Benchmark died: `httpx` dependency missing |
| `fannkuch` | 284 ms ± 3 ms |
| `float` | 80.0 ms ± 2.8 ms |
| `go` | 926 ms ± 10 ms |
| `hexiom` | 47.4 ms ± 0.4 ms |
| `json_dumps` | 34.6 ms ± 11.0 ms |
| `json_loads` | 79.9 µs ± 1.4 µs |
| `logging_format` | 96.2 µs ± 11.0 µs |
| `logging_silent` | 1.14 µs ± 0.01 µs |
| `logging_simple` | 87.2 µs ± 3.8 µs |
| `mdp` | 300 s timeout |
| `meteor_contest` | 1.23 s ± 0.06 s |
| `nbody` | 309 ms ± 12 ms |
| `nqueens` | 856 ms ± 19 ms |
| `pathlib` | 569 ms ± 14 ms |
| `pickle` | 17.6 µs ± 0.1 µs |
| `pickle_dict` | 23.3 µs ± 0.3 µs |
| `pickle_list` | 3.94 µs ± 0.06 µs |
| `pickle_pure_python` | 3.70 ms ± 0.07 ms |
| `pidigits` | 220 ms ± 3 ms |
| `pprint` / `pprint_safe_repr` | 8.57 s ± 0.05 s (definition later timed out) |
| `pyflate` | 2.68 s ± 0.05 s |
| `python_startup` | 33.5 ms ± 0.8 ms |
| `python_startup_no_site` | 20.4 ms ± 0.4 ms |
| `raytrace` | 2.09 s ± 0.03 s |
| `regex_compile` | 1.23 s ± 0.03 s |
| `regex_dna` | 210 ms ± 2 ms |
| `regex_effbot` | 12.5 ms ± 0.2 ms |
| `regex_v8` | 62.5 ms ± 0.8 ms |
| `richards` | 337 ms ± 3 ms |
| `richards_super` | 415 ms ± 4 ms |
| `scimark_fft` | 965 ms ± 9 ms |
| `scimark_lu` | 1.02 s ± 0.01 s |
| `scimark_monte_carlo` | 544 ms ± 3 ms |
| `scimark_sor` | 845 ms ± 16 ms |
| `scimark_sparse_mat_mult` | 17.4 ms ± 0.2 ms |
| `spectral_norm` | 536 ms ± 3 ms |
| `sqlite_synth` | 6.04 µs ± 0.10 µs |
| `telco` | 41.2 ms ± 1.1 ms |
| `typing_runtime_protocols` | 2.77 ms ± 0.08 ms |
| `unpack_sequence` | 161 ns ± 4 ns |
| `unpickle` | 64.5 µs ± 1.6 µs |
| `unpickle_list` | 10.9 µs ± 0.1 µs |
| `unpickle_pure_python` | 2.25 ms ± 0.04 ms |
| `xdsl` | Benchmark died: `xdsl` dependency missing |
| `networkx` | Benchmark died: `networkx` dependency missing |
| `networkx_connected_components` | Benchmark died: `networkx` dependency missing |
| `networkx_k_core` | Benchmark died: `networkx` dependency missing |

Pyperf warned that most quick samples lacked enough values for a 1% stability
claim; two reported standard deviations above 10% of the mean. These are
screening values and must not be treated as precise paired speedups. The
matched `async_tree_eager_memoization_tg` time (5.46 s) is effectively the
same as the earlier full run's 5.46 s; there is no confirmed improvement for
that case. The non-task-group memoization result is a different benchmark and
must not be compared with that task-group measurement.

The completed non-task-group `async_tree_memoization` result is 5.52 s ±
0.97 s, with pyperf reporting an 18% standard deviation and a 9.53-second
maximum. The previous full run recorded 5.09 s ± 0.38 s for the corresponding
case, so this run provides no evidence of a gain; the unstable samples prevent
a precise regression claim.

`async_tree_memoization_tg` completed at 5.29 s ± 0.38 s. The earlier result
was 5.13 s ± 0.35 s, a small difference inside noisy fast-mode measurements;
there is no confirmed gain or material regression for this matched variant.

`async_tree_none_tg` completed at 3.76 s ± 0.20 s, near the earlier 3.63 s ±
0.29 s result; this also provides no confirmed gain.

`asyncio_tcp` completed at 4.03 s ± 0.29 s, close to the previous 3.92 s ±
0.23 s result. The fast-mode samples do not establish a change.

`asyncio_tcp_ssl` timed out at the 300-second cap. `asyncio_websockets` died
during setup with `ModuleNotFoundError: No module named 'websockets'`; this is
an environment dependency failure, not a timing result or an XLang3 score.
The `base64` definition hit the 300-second cap. Its partial pyperf data is
preserved at
[`base64-543e9d1efa788584.json`](data/pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r3.pyperf-partial/base64-543e9d1efa788584.json)
with a provenance sidecar. `base64_small`, `base64_large`,
`urlsafe_base64_small`, `base32_small`, `base32_large`, `base16_small`,
`base16_large`, and `ascii85_small` completed before timeout. The `base32_large` path is pure Python;
its 1.85-second result is a strong generic-runtime target, but the matched
CPython ratio must wait for final JSON comparison. `bpe_tokeniser` also hit
the 300-second cap without a partial result. `chameleon` failed before timing
because its dependency is absent from the shared site-packages. The runner
completed `concurrent_imap`, `coroutines`, `fannkuch`, and `float`; setup also
failed for missing `coverage`, `pyaes`, `httpx`, `html5lib`, `mako`,
`networkx` (three definitions), `sphinx`, `sqlalchemy` (two definitions),
`sqlglot` (three definitions), `sympy`, `tomli`, `tornado`, and `xdsl`.
`mdp` exceeded its five-minute cap in a pure-Python dynamic-programming
workload that heavily exercises `fractions.Fraction`; it is a high-priority
generic runtime/arithmetic investigation target. The runner has advanced to
`pprint` also hit the five-minute definition cap after `pprint_safe_repr`
completed at 8.57 s ± 0.05 s. Its partial result and provenance are saved at
[`pprint-09c37ba9ede710b6.json`](data/pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r3.pyperf-partial/pprint-09c37ba9ede710b6.json).
`typing_runtime_protocols` completed at 2.77 ms ± 0.08 ms; an earlier focused
CPython 3.14.7 measurement was about 125 µs, leaving this workload roughly
22× slower despite the small constructor fast path. The runner has advanced
to `xml_etree` (case 97/97).

A generic small-integer `int.from_bytes`/`int.to_bytes` candidate is prepared
in the working tree, along with coverage for endian order, signed five-byte
values, bytes/bytearray/memoryview inputs, booleans, and the wider BigInt
fallback. It has not been built or measured; the active full run must finish
first so its remaining cases all use one unchanged Release DLL.

The suite remains active. Add the final 97-case status, matched CPython table,
aggregate, and horizontal ratio chart only after its pyperf output has
completed and passed validation.
