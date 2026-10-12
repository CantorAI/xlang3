# XLang3 before/after comparison on the same subtests

This comparison includes **68** subtests with completed measurements in both XLang3 runs and the saved CPython 3.14.7 reference. Newly completed cases and failures do not enter these speed ratios.

On this same set, geometric mean speed relative to CPython is **0.14152× before** and **0.13734× after**. The geometric mean old-XLang3/new-XLang3 ratio is **0.97049×**; above 1× means the new XLang3 run is faster.

These are arithmetic means of recorded pyperf measurement values; calibration and warmups are excluded. Fast-mode runs contain stability warnings and are not a paired statistical experiment. Nominal ratios do not prove a significant gain, August-performance restoration, or a win across all 97 definitions. See the separate full-run report for complete statuses and timing-population changes.

Nominal CPython wins on this shared set: 5 before, 4 after.

CPython-matched after results excluded because they lack a matching before result: scimark_fft, scimark_lu, scimark_monte_carlo, scimark_sor, scimark_sparse_mat_mult.

CPython-matched before results excluded because they lack a matching after result: async_tree_cpu_io_mixed, async_tree_cpu_io_mixed_tg.

Inputs: [before](pyperformance-xlang3-native-string-checkpoint-full-fast-20261007.json), [after](pyperformance-xlang3-subscription-dispatch-full-fast-20261007.json), [CPython 3.14.7](pyperformance-cpython314-clean-release-full-fast-20261002.json), [completed-run provenance](pyperformance-xlang3-subscription-dispatch-full-fast-20261007-provenance.json). Raw-input hashes and common-set statistics are in [subscription-dispatch-full-common-subtests-20261007.json](subscription-dispatch-full-common-subtests-20261007.json); all measurement counts are retained in [subscription-dispatch-full-common-subtests-20261007.csv](subscription-dispatch-full-common-subtests-20261007.csv).

| Subtest | CPython 3.14.7 | XLang3 before | XLang3 after | CP/before | CP/after | Before/after |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `2to3` | 346.7 ms | 2812 ms | 2940 ms | 0.1233× | 0.1179× | 0.9567× |
| `async_generators` | 287.5 ms | 2442 ms | 2561 ms | 0.1177× | 0.1123× | 0.9535× |
| `async_tree_eager` | 86.62 ms | 1489 ms | 1511 ms | 0.0582× | 0.0573× | 0.9855× |
| `async_tree_eager_cpu_io_mixed` | 336.2 ms | 6801 ms | 7131 ms | 0.0494× | 0.0471× | 0.9537× |
| `async_tree_eager_memoization` | 189.1 ms | 3850 ms | 3990 ms | 0.0491× | 0.0474× | 0.9651× |
| `async_tree_eager_memoization_tg` | 261.1 ms | 6141 ms | 6191 ms | 0.0425× | 0.0422× | 0.9920× |
| `async_tree_eager_tg` | 196.3 ms | 3742 ms | 3821 ms | 0.0525× | 0.0514× | 0.9792× |
| `async_tree_memoization` | 280.8 ms | 5809 ms | 5867 ms | 0.0483× | 0.0479× | 0.9901× |
| `async_tree_memoization_tg` | 281.8 ms | 5788 ms | 5985 ms | 0.0487× | 0.0471× | 0.9671× |
| `async_tree_none` | 227.4 ms | 4043 ms | 4320 ms | 0.0562× | 0.0526× | 0.9358× |
| `async_tree_none_tg` | 238.3 ms | 3955 ms | 3937 ms | 0.0603× | 0.0605× | 1.0046× |
| `asyncio_tcp` | 741.4 ms | 4237 ms | 4385 ms | 0.1750× | 0.1691× | 0.9661× |
| `asyncio_websockets` | 188.7 ms | 498.9 ms | 508.9 ms | 0.3783× | 0.3708× | 0.9803× |
| `chaos` | 47.75 ms | 462.1 ms | 462.2 ms | 0.1033× | 0.1033× | 0.9999× |
| `comprehensions` | 14.22 µs | 167.8 µs | 181.1 µs | 0.0847× | 0.0785× | 0.9266× |
| `connected_components` | 423.2 ms | 1626 ms | 1443 ms | 0.2603× | 0.2932× | 1.1265× |
| `coroutines` | 17.86 ms | 137.5 ms | 144.8 ms | 0.1299× | 0.1233× | 0.9494× |
| `crypto_pyaes` | 67.92 ms | 342.4 ms | 360.7 ms | 0.1983× | 0.1883× | 0.9493× |
| `deepcopy` | 217.8 µs | 2.615 ms | 2.74 ms | 0.0833× | 0.0795× | 0.9543× |
| `deepcopy_memo` | 23.65 µs | 271.4 µs | 288.4 µs | 0.0871× | 0.0820× | 0.9413× |
| `deepcopy_reduce` | 2.314 µs | 28.1 µs | 30.19 µs | 0.0824× | 0.0767× | 0.9308× |
| `deltablue` | 3.004 ms | 38.46 ms | 41.45 ms | 0.0781× | 0.0725× | 0.9279× |
| `fannkuch` | 324 ms | 275.8 ms | 287.4 ms | 1.1748× | 1.1273× | 0.9596× |
| `float` | 57.95 ms | 80.41 ms | 80.89 ms | 0.7208× | 0.7164× | 0.9940× |
| `gc_traversal` | 2.332 ms | 1.312 ms | 1.351 ms | 1.7782× | 1.7266× | 0.9710× |
| `generators` | 31.42 ms | 369.4 ms | 326.8 ms | 0.0851× | 0.0961× | 1.1301× |
| `go` | 103.7 ms | 925.8 ms | 960.4 ms | 0.1120× | 0.1079× | 0.9640× |
| `hexiom` | 5.44 ms | 47.42 ms | 48.2 ms | 0.1147× | 0.1129× | 0.9839× |
| `html5lib` | 49.61 ms | 771.2 ms | 796.7 ms | 0.0643× | 0.0623× | 0.9680× |
| `json_dumps` | 8.063 ms | 33.55 ms | 36.9 ms | 0.2403× | 0.2185× | 0.9093× |
| `json_loads` | 19.33 µs | 83.92 µs | 81.81 µs | 0.2303× | 0.2363× | 1.0258× |
| `logging_format` | 9.755 µs | 93.04 µs | 107.6 µs | 0.1049× | 0.0906× | 0.8643× |
| `logging_silent` | 70.03 ns | 1.035 µs | 1.113 µs | 0.0677× | 0.0629× | 0.9297× |
| `logging_simple` | 7.514 µs | 89.21 µs | 95.53 µs | 0.0842× | 0.0787× | 0.9339× |
| `many_optionals` | 667.9 µs | 8.62 ms | 9.596 ms | 0.0775× | 0.0696× | 0.8982× |
| `meteor_contest` | 120.2 ms | 1319 ms | 1480 ms | 0.0912× | 0.0812× | 0.8908× |
| `nbody` | 90.4 ms | 301.1 ms | 317 ms | 0.3003× | 0.2852× | 0.9498× |
| `nqueens` | 76.93 ms | 885.5 ms | 898.9 ms | 0.0869× | 0.0856× | 0.9851× |
| `pathlib` | 50.56 ms | 619.1 ms | 654.8 ms | 0.0817× | 0.0772× | 0.9455× |
| `pickle` | 9.839 µs | 19.89 µs | 20.21 µs | 0.4947× | 0.4869× | 0.9843× |
| `pickle_dict` | 26.41 µs | 25.59 µs | 26.54 µs | 1.0323× | 0.9952× | 0.9641× |
| `pickle_list` | 4.625 µs | 4.089 µs | 4.47 µs | 1.1311× | 1.0346× | 0.9146× |
| `pickle_pure_python` | 273.5 µs | 5.562 ms | 5.742 ms | 0.0492× | 0.0476× | 0.9686× |
| `pidigits` | 164.8 ms | 401.1 ms | 420.9 ms | 0.4110× | 0.3916× | 0.9529× |
| `pyflate` | 366.5 ms | 3135 ms | 3203 ms | 0.1169× | 0.1144× | 0.9788× |
| `python_startup` | 24.94 ms | 32.38 ms | 32.89 ms | 0.7702× | 0.7582× | 0.9844× |
| `python_startup_no_site` | 20.68 ms | 19.26 ms | 19.13 ms | 1.0737× | 1.0815× | 1.0073× |
| `raytrace` | 272.2 ms | 2401 ms | 2308 ms | 0.1133× | 0.1179× | 1.0405× |
| `regex_compile` | 118.6 ms | 1369 ms | 1391 ms | 0.0867× | 0.0853× | 0.9842× |
| `regex_dna` | 142.7 ms | 224.3 ms | 221.2 ms | 0.6361× | 0.6451× | 1.0140× |
| `regex_effbot` | 1.995 ms | 12.88 ms | 13.39 ms | 0.1550× | 0.1491× | 0.9619× |
| `regex_v8` | 18.81 ms | 65.13 ms | 71.22 ms | 0.2888× | 0.2641× | 0.9144× |
| `richards` | 36.6 ms | 377.8 ms | 385.9 ms | 0.0969× | 0.0948× | 0.9789× |
| `richards_super` | 41.37 ms | 514.5 ms | 534.5 ms | 0.0804× | 0.0774× | 0.9625× |
| `shortest_path` | 470 ms | 1738 ms | 1589 ms | 0.2704× | 0.2957× | 1.0937× |
| `spectral_norm` | 103.8 ms | 517.3 ms | 522.5 ms | 0.2007× | 0.1986× | 0.9899× |
| `sqlglot_v2_normalize` | 98.39 ms | 1784 ms | 1780 ms | 0.0551× | 0.0553× | 1.0023× |
| `sqlglot_v2_optimize` | 41.61 ms | 802.7 ms | 803.1 ms | 0.0518× | 0.0518× | 0.9994× |
| `sqlglot_v2_parse` | 1.01 ms | 22.93 ms | 22.87 ms | 0.0441× | 0.0442× | 1.0025× |
| `sqlglot_v2_transpile` | 1.302 ms | 27.22 ms | 27.42 ms | 0.0478× | 0.0475× | 0.9929× |
| `subparsers` | 8.151 ms | 147.5 ms | 149.7 ms | 0.0552× | 0.0544× | 0.9854× |
| `telco` | 5.755 ms | 44.59 ms | 43.25 ms | 0.1291× | 0.1331× | 1.0309× |
| `tornado_http` | 274.7 ms | 1481 ms | 1608 ms | 0.1855× | 0.1708× | 0.9211× |
| `typing_runtime_protocols` | 132.4 µs | 250.3 µs | 252.7 µs | 0.5289× | 0.5239× | 0.9905× |
| `unpack_sequence` | 44.67 ns | 162.5 ns | 178.7 ns | 0.2749× | 0.2500× | 0.9093× |
| `unpickle` | 10.58 µs | 70.56 µs | 75.81 µs | 0.1500× | 0.1396× | 0.9307× |
| `unpickle_list` | 3.365 µs | 11.83 µs | 11.94 µs | 0.2844× | 0.2818× | 0.9908× |
| `unpickle_pure_python` | 205.3 µs | 2.618 ms | 2.683 ms | 0.0784× | 0.0765× | 0.9758× |
