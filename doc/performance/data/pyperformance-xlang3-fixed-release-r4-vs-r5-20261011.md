# XLang3 before/after comparison on the same subtests

This comparison includes **70** subtests with completed measurements in both XLang3 runs and the saved CPython 3.14.7 reference. Newly completed cases and failures do not enter these speed ratios.

On this same set, geometric mean speed relative to CPython is **0.14013× before** and **0.14125× after**. The geometric mean old-XLang3/new-XLang3 ratio is **1.00799×**; above 1× means the new XLang3 run is faster.

These are arithmetic means of recorded pyperf measurement values; calibration and warmups are excluded. Fast-mode runs contain stability warnings and are not a paired statistical experiment. Nominal ratios do not prove a significant gain, August-performance restoration, or a win across all 97 definitions. See the separate full-run report for complete statuses and timing-population changes.

Nominal CPython wins on this shared set: 2 before, 1 after.

CPython-matched after results excluded because they lack a matching before result: none.

CPython-matched before results excluded because they lack a matching after result: none.

Inputs: [before](pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r4.pyperf.json), [after](pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r5.pyperf.json), [CPython 3.14.7](pyperformance-cpython3147-live-eval-full-fast-20261007.json), [completed-run provenance](pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r5.provenance.json). Raw-input hashes and common-set statistics are in [pyperformance-xlang3-fixed-release-r4-vs-r5-20261011.json](pyperformance-xlang3-fixed-release-r4-vs-r5-20261011.json); all measurement counts are retained in [pyperformance-xlang3-fixed-release-r4-vs-r5-20261011.csv](pyperformance-xlang3-fixed-release-r4-vs-r5-20261011.csv).

| Subtest | CPython 3.14.7 | XLang3 before | XLang3 after | CP/before | CP/after | Before/after |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `2to3` | 315 ms | 2697 ms | 2818 ms | 0.1168× | 0.1118× | 0.9572× |
| `async_generators` | 286.2 ms | 2461 ms | 2594 ms | 0.1163× | 0.1103× | 0.9487× |
| `async_tree_cpu_io_mixed` | 428 ms | 5373 ms | 5795 ms | 0.0797× | 0.0739× | 0.9272× |
| `async_tree_cpu_io_mixed_tg` | 425 ms | 5179 ms | 5309 ms | 0.0821× | 0.0801× | 0.9754× |
| `async_tree_eager` | 85.71 ms | 1416 ms | 1473 ms | 0.0605× | 0.0582× | 0.9611× |
| `async_tree_eager_cpu_io_mixed` | 338.2 ms | 2852 ms | 2975 ms | 0.1186× | 0.1137× | 0.9586× |
| `async_tree_eager_cpu_io_mixed_tg` | 385.4 ms | 5214 ms | 4954 ms | 0.0739× | 0.0778× | 1.0524× |
| `async_tree_eager_memoization` | 187.5 ms | 3515 ms | 3402 ms | 0.0534× | 0.0551× | 1.0332× |
| `async_tree_eager_memoization_tg` | 266 ms | 5674 ms | 5742 ms | 0.0469× | 0.0463× | 0.9882× |
| `async_tree_eager_tg` | 193.1 ms | 3612 ms | 3568 ms | 0.0535× | 0.0541× | 1.0122× |
| `async_tree_memoization` | 278.5 ms | 5496 ms | 5129 ms | 0.0507× | 0.0543× | 1.0715× |
| `async_tree_memoization_tg` | 275.8 ms | 5527 ms | 5426 ms | 0.0499× | 0.0508× | 1.0186× |
| `async_tree_none` | 229.3 ms | 4986 ms | 4291 ms | 0.0460× | 0.0534× | 1.1619× |
| `async_tree_none_tg` | 239.4 ms | 3837 ms | 3694 ms | 0.0624× | 0.0648× | 1.0385× |
| `asyncio_tcp` | 585.3 ms | 5069 ms | 3953 ms | 0.1155× | 0.1481× | 1.2823× |
| `bench_mp_pool` | 153.9 ms | 336.4 ms | 334.6 ms | 0.4573× | 0.4599× | 1.0056× |
| `bench_thread_pool` | 1.007 ms | 9.801 ms | 9.683 ms | 0.1027× | 0.1040× | 1.0121× |
| `chaos` | 48.17 ms | 450.7 ms | 446.8 ms | 0.1069× | 0.1078× | 1.0088× |
| `comprehensions` | 13.96 µs | 170.8 µs | 173.6 µs | 0.0818× | 0.0804× | 0.9836× |
| `coroutines` | 17.41 ms | 144.8 ms | 144.2 ms | 0.1202× | 0.1208× | 1.0045× |
| `create_gc_cycles` | 1.469 ms | 2.095 ms | 2.177 ms | 0.7009× | 0.6747× | 0.9625× |
| `deepcopy` | 210.8 µs | 2.475 ms | 2.457 ms | 0.0852× | 0.0858× | 1.0071× |
| `deepcopy_memo` | 21.83 µs | 267.2 µs | 269.1 µs | 0.0817× | 0.0811× | 0.9929× |
| `deepcopy_reduce` | 2.213 µs | 25.79 µs | 25.07 µs | 0.0858× | 0.0883× | 1.0284× |
| `deltablue` | 2.516 ms | 40.28 ms | 39.51 ms | 0.0625× | 0.0637× | 1.0196× |
| `fannkuch` | 317.2 ms | 281.9 ms | 281.1 ms | 1.1250× | 1.1283× | 1.0030× |
| `float` | 55.2 ms | 78.64 ms | 79.96 ms | 0.7020× | 0.6904× | 0.9835× |
| `gc_traversal` | 2.14 ms | 2.111 ms | 2.245 ms | 1.0137× | 0.9534× | 0.9405× |
| `generators` | 26.63 ms | 327.6 ms | 320.7 ms | 0.0813× | 0.0830× | 1.0213× |
| `go` | 95.73 ms | 944.4 ms | 930.8 ms | 0.1014× | 0.1029× | 1.0147× |
| `hexiom` | 5.091 ms | 47.56 ms | 46.91 ms | 0.1070× | 0.1085× | 1.0140× |
| `json_dumps` | 7.636 ms | 28.85 ms | 28.4 ms | 0.2647× | 0.2689× | 1.0160× |
| `json_loads` | 18.1 µs | 80.52 µs | 79.15 µs | 0.2248× | 0.2287× | 1.0172× |
| `logging_format` | 7.551 µs | 93.97 µs | 94.14 µs | 0.0804× | 0.0802× | 0.9982× |
| `logging_silent` | 69.12 ns | 1.285 µs | 1.162 µs | 0.0538× | 0.0595× | 1.1060× |
| `logging_simple` | 6.937 µs | 84.79 µs | 85.92 µs | 0.0818× | 0.0807× | 0.9868× |
| `many_optionals` | 638.1 µs | 8.385 ms | 8.448 ms | 0.0761× | 0.0755× | 0.9925× |
| `meteor_contest` | 89.46 ms | 1263 ms | 1213 ms | 0.0708× | 0.0738× | 1.0414× |
| `nbody` | 82.85 ms | 334.6 ms | 311.3 ms | 0.2476× | 0.2661× | 1.0749× |
| `nqueens` | 76.88 ms | 903.6 ms | 814.7 ms | 0.0851× | 0.0944× | 1.1092× |
| `pathlib` | 42.31 ms | 656.2 ms | 626.2 ms | 0.0645× | 0.0676× | 1.0480× |
| `pickle` | 9.425 µs | 24.6 µs | 23.8 µs | 0.3831× | 0.3961× | 1.0338× |
| `pickle_dict` | 23.59 µs | 25.2 µs | 25.6 µs | 0.9364× | 0.9215× | 0.9841× |
| `pickle_list` | 4.024 µs | 4.12 µs | 4.168 µs | 0.9768× | 0.9655× | 0.9885× |
| `pickle_pure_python` | 257.5 µs | 3.585 ms | 3.55 ms | 0.0718× | 0.0725× | 1.0098× |
| `pidigits` | 163 ms | 229.7 ms | 223.6 ms | 0.7096× | 0.7290× | 1.0273× |
| `pyflate` | 344.9 ms | 2726 ms | 2691 ms | 0.1265× | 0.1282× | 1.0131× |
| `python_startup` | 18.6 ms | 34.59 ms | 34.52 ms | 0.5376× | 0.5387× | 1.0021× |
| `python_startup_no_site` | 15.75 ms | 21.09 ms | 21.22 ms | 0.7470× | 0.7423× | 0.9936× |
| `raytrace` | 227.3 ms | 2104 ms | 2081 ms | 0.1080× | 0.1092× | 1.0111× |
| `regex_compile` | 98.07 ms | 1193 ms | 1193 ms | 0.0822× | 0.0822× | 1.0002× |
| `regex_dna` | 137.4 ms | 211.4 ms | 214.3 ms | 0.6501× | 0.6411× | 0.9861× |
| `regex_effbot` | 1.86 ms | 12.57 ms | 13.21 ms | 0.1480× | 0.1409× | 0.9519× |
| `regex_v8` | 15.72 ms | 63.36 ms | 65.05 ms | 0.2481× | 0.2416× | 0.9739× |
| `richards` | 34.14 ms | 334.6 ms | 330.5 ms | 0.1020× | 0.1033× | 1.0125× |
| `richards_super` | 38.41 ms | 404.1 ms | 397.3 ms | 0.0951× | 0.0967× | 1.0172× |
| `scimark_fft` | 227.4 ms | 974.7 ms | 982.1 ms | 0.2333× | 0.2315× | 0.9924× |
| `scimark_lu` | 75.29 ms | 303.3 ms | 300.4 ms | 0.2483× | 0.2507× | 1.0096× |
| `scimark_monte_carlo` | 52.83 ms | 524.3 ms | 515.1 ms | 0.1008× | 0.1026× | 1.0179× |
| `scimark_sor` | 97.53 ms | 854.2 ms | 856.5 ms | 0.1142× | 0.1139× | 0.9974× |
| `scimark_sparse_mat_mult` | 3.144 ms | 17.88 ms | 17.91 ms | 0.1759× | 0.1755× | 0.9981× |
| `spectral_norm` | 75.79 ms | 528.6 ms | 531.5 ms | 0.1434× | 0.1426× | 0.9944× |
| `sqlite_synth` | 1.833 µs | 6.161 µs | 6.175 µs | 0.2975× | 0.2968× | 0.9977× |
| `subparsers` | 7.881 ms | 133.9 ms | 138.7 ms | 0.0588× | 0.0568× | 0.9657× |
| `telco` | 5.71 ms | 41.72 ms | 41.41 ms | 0.1369× | 0.1379× | 1.0074× |
| `typing_runtime_protocols` | 128.1 µs | 2.861 ms | 2.821 ms | 0.0448× | 0.0454× | 1.0143× |
| `unpack_sequence` | 36.58 ns | 163.2 ns | 161.6 ns | 0.2242× | 0.2263× | 1.0094× |
| `unpickle` | 9.832 µs | 65.97 µs | 69.37 µs | 0.1490× | 0.1417× | 0.9510× |
| `unpickle_list` | 3.215 µs | 11.12 µs | 11.84 µs | 0.2890× | 0.2716× | 0.9397× |
| `unpickle_pure_python` | 168.3 µs | 2.235 ms | 2.32 ms | 0.0753× | 0.0725× | 0.9634× |
