# XLang3 before/after comparison on the same subtests

This comparison includes **56** subtests with completed measurements in both XLang3 runs and the saved CPython 3.14.7 reference. Newly completed cases and failures do not enter these speed ratios.

On this same set, geometric mean speed relative to CPython is **0.15654× before** and **0.16192× after**. The geometric mean old-XLang3/new-XLang3 ratio is **1.03435×**; above 1× means the new XLang3 run is faster.

These are arithmetic means of recorded pyperf measurement values; calibration and warmups are excluded. Fast-mode runs contain stability warnings and are not a paired statistical experiment. Nominal ratios do not prove a significant gain, August-performance restoration, or a win across all 97 definitions. See the separate full-run report for complete statuses and timing-population changes.

Nominal CPython wins on this shared set: 5 before, 5 after.

CPython-matched after results excluded because they lack a matching before result: async_tree_cpu_io_mixed, async_tree_cpu_io_mixed_tg, async_tree_eager_cpu_io_mixed, async_tree_eager_memoization, async_tree_eager_memoization_tg, async_tree_eager_tg, async_tree_memoization, async_tree_memoization_tg, async_tree_none, async_tree_none_tg, asyncio_tcp, connected_components, html5lib, shortest_path.

CPython-matched before results excluded because they lack a matching after result: none.

Inputs: [before](data/pyperformance-xlang3-bytearray-iadd-full-fast-20261006.json), [after](data/pyperformance-xlang3-native-string-checkpoint-full-fast-20261007.json), [CPython 3.14.7](data/pyperformance-cpython314-clean-release-full-fast-20261002.json), [completed-run provenance](data/pyperformance-xlang3-native-string-checkpoint-full-fast-20261007-provenance.json). Raw-input hashes and common-set statistics are in [pyperformance-xlang3-native-string-checkpoint-common-subtests-20261007.json](pyperformance-xlang3-native-string-checkpoint-common-subtests-20261007.json); all measurement counts are retained in [pyperformance-xlang3-native-string-checkpoint-common-subtests-20261007.csv](pyperformance-xlang3-native-string-checkpoint-common-subtests-20261007.csv).

| Subtest | CPython 3.14.7 | XLang3 before | XLang3 after | CP/before | CP/after | Before/after |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `2to3` | 346.7 ms | 2953 ms | 2812 ms | 0.1174× | 0.1233× | 1.0499× |
| `async_generators` | 287.5 ms | 2505 ms | 2442 ms | 0.1148× | 0.1177× | 1.0257× |
| `async_tree_eager` | 86.62 ms | 1516 ms | 1489 ms | 0.0572× | 0.0582× | 1.0177× |
| `asyncio_websockets` | 188.7 ms | 507.3 ms | 498.9 ms | 0.3721× | 0.3783× | 1.0167× |
| `chaos` | 47.75 ms | 472.4 ms | 462.1 ms | 0.1011× | 0.1033× | 1.0223× |
| `comprehensions` | 14.22 µs | 180 µs | 167.8 µs | 0.0790× | 0.0847× | 1.0723× |
| `coroutines` | 17.86 ms | 146.3 ms | 137.5 ms | 0.1221× | 0.1299× | 1.0637× |
| `crypto_pyaes` | 67.92 ms | 358.1 ms | 342.4 ms | 0.1897× | 0.1983× | 1.0457× |
| `deepcopy` | 217.8 µs | 2.771 ms | 2.615 ms | 0.0786× | 0.0833× | 1.0598× |
| `deepcopy_memo` | 23.65 µs | 283.9 µs | 271.4 µs | 0.0833× | 0.0871× | 1.0459× |
| `deepcopy_reduce` | 2.314 µs | 30.33 µs | 28.1 µs | 0.0763× | 0.0824× | 1.0793× |
| `deltablue` | 3.004 ms | 42.91 ms | 38.46 ms | 0.0700× | 0.0781× | 1.1157× |
| `fannkuch` | 324 ms | 297.1 ms | 275.8 ms | 1.0908× | 1.1748× | 1.0769× |
| `float` | 57.95 ms | 81.83 ms | 80.41 ms | 0.7083× | 0.7208× | 1.0177× |
| `gc_traversal` | 2.332 ms | 1.362 ms | 1.312 ms | 1.7128× | 1.7782× | 1.0382× |
| `generators` | 31.42 ms | 347.4 ms | 369.4 ms | 0.0904× | 0.0851× | 0.9406× |
| `go` | 103.7 ms | 973.4 ms | 925.8 ms | 0.1065× | 0.1120× | 1.0514× |
| `hexiom` | 5.44 ms | 48.88 ms | 47.42 ms | 0.1113× | 0.1147× | 1.0307× |
| `json_dumps` | 8.063 ms | 37.19 ms | 33.55 ms | 0.2168× | 0.2403× | 1.1084× |
| `json_loads` | 19.33 µs | 95.53 µs | 83.92 µs | 0.2023× | 0.2303× | 1.1383× |
| `logging_format` | 9.755 µs | 103.1 µs | 93.04 µs | 0.0947× | 0.1049× | 1.1078× |
| `logging_silent` | 70.03 ns | 1.169 µs | 1.035 µs | 0.0599× | 0.0677× | 1.1298× |
| `logging_simple` | 7.514 µs | 95.79 µs | 89.21 µs | 0.0784× | 0.0842× | 1.0737× |
| `many_optionals` | 667.9 µs | 9.424 ms | 8.62 ms | 0.0709× | 0.0775× | 1.0933× |
| `meteor_contest` | 120.2 ms | 1352 ms | 1319 ms | 0.0889× | 0.0912× | 1.0254× |
| `nbody` | 90.4 ms | 324.4 ms | 301.1 ms | 0.2786× | 0.3003× | 1.0776× |
| `nqueens` | 76.93 ms | 876.2 ms | 885.5 ms | 0.0878× | 0.0869× | 0.9895× |
| `pathlib` | 50.56 ms | 616.6 ms | 619.1 ms | 0.0820× | 0.0817× | 0.9960× |
| `pickle` | 9.839 µs | 19.44 µs | 19.89 µs | 0.5061× | 0.4947× | 0.9774× |
| `pickle_dict` | 26.41 µs | 25.91 µs | 25.59 µs | 1.0195× | 1.0323× | 1.0126× |
| `pickle_list` | 4.625 µs | 4.233 µs | 4.089 µs | 1.0925× | 1.1311× | 1.0353× |
| `pickle_pure_python` | 273.5 µs | 5.734 ms | 5.562 ms | 0.0477× | 0.0492× | 1.0311× |
| `pidigits` | 164.8 ms | 395.8 ms | 401.1 ms | 0.4164× | 0.4110× | 0.9869× |
| `pyflate` | 366.5 ms | 3125 ms | 3135 ms | 0.1173× | 0.1169× | 0.9968× |
| `python_startup` | 24.94 ms | 33.71 ms | 32.38 ms | 0.7398× | 0.7702× | 1.0411× |
| `python_startup_no_site` | 20.68 ms | 19.44 ms | 19.26 ms | 1.0638× | 1.0737× | 1.0093× |
| `raytrace` | 272.2 ms | 2381 ms | 2401 ms | 0.1143× | 0.1133× | 0.9917× |
| `regex_compile` | 118.6 ms | 1326 ms | 1369 ms | 0.0894× | 0.0867× | 0.9689× |
| `regex_dna` | 142.7 ms | 224.1 ms | 224.3 ms | 0.6369× | 0.6361× | 0.9988× |
| `regex_effbot` | 1.995 ms | 13.31 ms | 12.88 ms | 0.1499× | 0.1550× | 1.0338× |
| `regex_v8` | 18.81 ms | 67.14 ms | 65.13 ms | 0.2802× | 0.2888× | 1.0310× |
| `richards` | 36.6 ms | 369.5 ms | 377.8 ms | 0.0991× | 0.0969× | 0.9780× |
| `richards_super` | 41.37 ms | 501 ms | 514.5 ms | 0.0826× | 0.0804× | 0.9737× |
| `spectral_norm` | 103.8 ms | 555.6 ms | 517.3 ms | 0.1868× | 0.2007× | 1.0741× |
| `sqlglot_v2_normalize` | 98.39 ms | 1767 ms | 1784 ms | 0.0557× | 0.0551× | 0.9903× |
| `sqlglot_v2_optimize` | 41.61 ms | 812.8 ms | 802.7 ms | 0.0512× | 0.0518× | 1.0127× |
| `sqlglot_v2_parse` | 1.01 ms | 23.41 ms | 22.93 ms | 0.0432× | 0.0441× | 1.0207× |
| `sqlglot_v2_transpile` | 1.302 ms | 27.4 ms | 27.22 ms | 0.0475× | 0.0478× | 1.0064× |
| `subparsers` | 8.151 ms | 163.1 ms | 147.5 ms | 0.0500× | 0.0552× | 1.1053× |
| `telco` | 5.755 ms | 44.52 ms | 44.59 ms | 0.1293× | 0.1291× | 0.9985× |
| `tornado_http` | 274.7 ms | 1510 ms | 1481 ms | 0.1819× | 0.1855× | 1.0197× |
| `typing_runtime_protocols` | 132.4 µs | 257.8 µs | 250.3 µs | 0.5136× | 0.5289× | 1.0297× |
| `unpack_sequence` | 44.67 ns | 173.2 ns | 162.5 ns | 0.2580× | 0.2749× | 1.0657× |
| `unpickle` | 10.58 µs | 71.85 µs | 70.56 µs | 0.1473× | 0.1500× | 1.0183× |
| `unpickle_list` | 3.365 µs | 12.72 µs | 11.83 µs | 0.2644× | 0.2844× | 1.0755× |
| `unpickle_pure_python` | 205.3 µs | 2.566 ms | 2.618 ms | 0.0800× | 0.0784× | 0.9804× |
