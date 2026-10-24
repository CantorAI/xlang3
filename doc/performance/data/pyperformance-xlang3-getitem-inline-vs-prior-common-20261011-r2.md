# XLang3 before/after comparison on the same subtests

This comparison includes **87** subtests with completed measurements in both XLang3 runs and the saved CPython 3.14.7 reference. Newly completed cases and failures do not enter these speed ratios.

On this same set, geometric mean speed relative to CPython is **0.12922× before** and **0.12775× after**. The geometric mean old-XLang3/new-XLang3 ratio is **0.98856×**; above 1× means the new XLang3 run is faster.

These are arithmetic means of recorded pyperf measurement values; calibration and warmups are excluded. Fast-mode runs contain stability warnings and are not a paired statistical experiment. Nominal ratios do not prove a significant gain, August-performance restoration, or a win across all 97 definitions. See the separate full-run report for complete statuses and timing-population changes.

Nominal CPython wins on this shared set: 2 before, 1 after.

CPython-matched after results excluded because they lack a matching before result: mako.

CPython-matched before results excluded because they lack a matching after result: none.

Inputs: [before](pyperformance-xlang3-fixed-release-full-fast-shimmed-20261011-r2.json), [after](pyperformance-xlang3-getitem-inline-full-fast-20261011-r2-combined.pyperf.json), [CPython 3.14.7](pyperformance-cpython3147-live-eval-full-fast-20261007.json), [completed-run provenance](pyperformance-xlang3-getitem-inline-full-fast-20261011-r2-combined.provenance.json). Raw-input hashes and common-set statistics are in [pyperformance-xlang3-getitem-inline-vs-prior-common-20261011-r2.json](pyperformance-xlang3-getitem-inline-vs-prior-common-20261011-r2.json); all measurement counts are retained in [pyperformance-xlang3-getitem-inline-vs-prior-common-20261011-r2.csv](pyperformance-xlang3-getitem-inline-vs-prior-common-20261011-r2.csv).

| Subtest | CPython 3.14.7 | XLang3 before | XLang3 after | CP/before | CP/after | Before/after |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `2to3` | 315 ms | 2705 ms | 2706 ms | 0.1164× | 0.1164× | 1.0000× |
| `async_generators` | 286.2 ms | 2411 ms | 2494 ms | 0.1187× | 0.1147× | 0.9666× |
| `async_tree_cpu_io_mixed` | 428 ms | 4804 ms | 4873 ms | 0.0891× | 0.0878× | 0.9858× |
| `async_tree_cpu_io_mixed_tg` | 425 ms | 4789 ms | 5670 ms | 0.0887× | 0.0750× | 0.8446× |
| `async_tree_eager` | 85.71 ms | 1352 ms | 1497 ms | 0.0634× | 0.0572× | 0.9028× |
| `async_tree_eager_cpu_io_mixed` | 338.2 ms | 2685 ms | 3214 ms | 0.1260× | 0.1052× | 0.8352× |
| `async_tree_eager_cpu_io_mixed_tg` | 385.4 ms | 4521 ms | 4996 ms | 0.0852× | 0.0771× | 0.9048× |
| `async_tree_eager_memoization` | 187.5 ms | 3356 ms | 3688 ms | 0.0559× | 0.0508× | 0.9098× |
| `async_tree_eager_memoization_tg` | 266 ms | 5457 ms | 5723 ms | 0.0487× | 0.0465× | 0.9535× |
| `async_tree_eager_tg` | 193.1 ms | 3323 ms | 3549 ms | 0.0581× | 0.0544× | 0.9364× |
| `async_tree_memoization` | 278.5 ms | 5672 ms | 5412 ms | 0.0491× | 0.0515× | 1.0481× |
| `async_tree_memoization_tg` | 275.8 ms | 5555 ms | 5271 ms | 0.0496× | 0.0523× | 1.0539× |
| `async_tree_none` | 229.3 ms | 3807 ms | 4008 ms | 0.0602× | 0.0572× | 0.9498× |
| `async_tree_none_tg` | 239.4 ms | 4020 ms | 3762 ms | 0.0595× | 0.0636× | 1.0684× |
| `asyncio_tcp` | 585.3 ms | 3745 ms | 3879 ms | 0.1563× | 0.1509× | 0.9654× |
| `asyncio_websockets` | 176.4 ms | 476.5 ms | 489.6 ms | 0.3701× | 0.3602× | 0.9732× |
| `bench_mp_pool` | 153.9 ms | 321.4 ms | 336.9 ms | 0.4787× | 0.4567× | 0.9541× |
| `bench_thread_pool` | 1.007 ms | 9.191 ms | 10.15 ms | 0.1095× | 0.0992× | 0.9059× |
| `chameleon` | 11.6 ms | 135.5 ms | 163.1 ms | 0.0856× | 0.0711× | 0.8309× |
| `chaos` | 48.17 ms | 451.7 ms | 457.1 ms | 0.1067× | 0.1054× | 0.9881× |
| `comprehensions` | 13.96 µs | 173.6 µs | 171.8 µs | 0.0804× | 0.0813× | 1.0104× |
| `connected_components` | 380.9 ms | 851.2 ms | 883.7 ms | 0.4475× | 0.4311× | 0.9633× |
| `coroutines` | 17.41 ms | 140.6 ms | 143.1 ms | 0.1238× | 0.1217× | 0.9826× |
| `coverage` | 65.51 ms | 702.1 ms | 694.5 ms | 0.0933× | 0.0943× | 1.0110× |
| `create_gc_cycles` | 1.469 ms | 2.265 ms | 2.208 ms | 0.6485× | 0.6652× | 1.0258× |
| `crypto_pyaes` | 57.55 ms | 338.7 ms | 340.3 ms | 0.1699× | 0.1691× | 0.9951× |
| `deepcopy` | 210.8 µs | 2.776 ms | 2.674 ms | 0.0759× | 0.0788× | 1.0382× |
| `deepcopy_memo` | 21.83 µs | 300.8 µs | 295 µs | 0.0726× | 0.0740× | 1.0198× |
| `deepcopy_reduce` | 2.213 µs | 27.58 µs | 27.67 µs | 0.0802× | 0.0800× | 0.9969× |
| `deltablue` | 2.516 ms | 39.91 ms | 40.09 ms | 0.0630× | 0.0627× | 0.9953× |
| `django_template` | 28.87 ms | 516.5 ms | 501.6 ms | 0.0559× | 0.0576× | 1.0297× |
| `fannkuch` | 317.2 ms | 285.4 ms | 280 ms | 1.1112× | 1.1330× | 1.0196× |
| `float` | 55.2 ms | 86.76 ms | 81.48 ms | 0.6363× | 0.6775× | 1.0648× |
| `gc_traversal` | 2.14 ms | 2.199 ms | 2.235 ms | 0.9732× | 0.9574× | 0.9838× |
| `generators` | 26.63 ms | 334.1 ms | 368.3 ms | 0.0797× | 0.0723× | 0.9072× |
| `genshi_text` | 19.37 ms | 335.1 ms | 316.4 ms | 0.0578× | 0.0612× | 1.0592× |
| `genshi_xml` | 42.5 ms | 913.2 ms | 876.7 ms | 0.0465× | 0.0485× | 1.0416× |
| `go` | 95.73 ms | 931.5 ms | 1012 ms | 0.1028× | 0.0946× | 0.9200× |
| `hexiom` | 5.091 ms | 47.38 ms | 53.42 ms | 0.1075× | 0.0953× | 0.8869× |
| `html5lib` | 42.62 ms | 653.4 ms | 680.6 ms | 0.0652× | 0.0626× | 0.9600× |
| `json_dumps` | 7.636 ms | 31.73 ms | 34.63 ms | 0.2407× | 0.2205× | 0.9163× |
| `json_loads` | 18.1 µs | 79.75 µs | 84.89 µs | 0.2270× | 0.2133× | 0.9394× |
| `logging_format` | 7.551 µs | 93.79 µs | 96.25 µs | 0.0805× | 0.0785× | 0.9745× |
| `logging_silent` | 69.12 ns | 1.119 µs | 1.142 µs | 0.0618× | 0.0605× | 0.9803× |
| `logging_simple` | 6.937 µs | 87.63 µs | 120.1 µs | 0.0792× | 0.0577× | 0.7294× |
| `many_optionals` | 638.1 µs | 8.067 ms | 8.774 ms | 0.0791× | 0.0727× | 0.9194× |
| `meteor_contest` | 89.46 ms | 1180 ms | 1228 ms | 0.0758× | 0.0728× | 0.9604× |
| `nbody` | 82.85 ms | 304.3 ms | 296.3 ms | 0.2723× | 0.2796× | 1.0268× |
| `nqueens` | 76.88 ms | 952.2 ms | 851.6 ms | 0.0807× | 0.0903× | 1.1181× |
| `pathlib` | 42.31 ms | 582.2 ms | 596.8 ms | 0.0727× | 0.0709× | 0.9755× |
| `pickle` | 9.425 µs | 17.89 µs | 18.42 µs | 0.5269× | 0.5118× | 0.9713× |
| `pickle_dict` | 23.59 µs | 24.28 µs | 24.51 µs | 0.9718× | 0.9626× | 0.9906× |
| `pickle_list` | 4.024 µs | 4.01 µs | 4.089 µs | 1.0036× | 0.9842× | 0.9806× |
| `pickle_pure_python` | 257.5 µs | 3.675 ms | 3.744 ms | 0.0701× | 0.0688× | 0.9816× |
| `pidigits` | 163 ms | 274.1 ms | 225.1 ms | 0.5945× | 0.7239× | 1.2177× |
| `pyflate` | 344.9 ms | 2772 ms | 3798 ms | 0.1244× | 0.0908× | 0.7300× |
| `python_startup` | 18.6 ms | 33.78 ms | 34.5 ms | 0.5505× | 0.5391× | 0.9793× |
| `python_startup_no_site` | 15.75 ms | 21.02 ms | 21.13 ms | 0.7495× | 0.7455× | 0.9947× |
| `raytrace` | 227.3 ms | 2238 ms | 2160 ms | 0.1016× | 0.1052× | 1.0358× |
| `regex_compile` | 98.07 ms | 1287 ms | 1294 ms | 0.0762× | 0.0758× | 0.9947× |
| `regex_dna` | 137.4 ms | 214.5 ms | 218.1 ms | 0.6406× | 0.6300× | 0.9835× |
| `regex_effbot` | 1.86 ms | 12.74 ms | 12.86 ms | 0.1461× | 0.1447× | 0.9906× |
| `regex_v8` | 15.72 ms | 65.1 ms | 64.39 ms | 0.2414× | 0.2441× | 1.0112× |
| `richards` | 34.14 ms | 346.9 ms | 340.6 ms | 0.0984× | 0.1002× | 1.0185× |
| `richards_super` | 38.41 ms | 414.3 ms | 412.8 ms | 0.0927× | 0.0931× | 1.0038× |
| `scimark_fft` | 227.4 ms | 959.7 ms | 941.2 ms | 0.2369× | 0.2416× | 1.0197× |
| `scimark_lu` | 75.29 ms | 1091 ms | 290 ms | 0.0690× | 0.2596× | 3.7626× |
| `scimark_monte_carlo` | 52.83 ms | 542.8 ms | 538.4 ms | 0.0973× | 0.0981× | 1.0083× |
| `scimark_sor` | 97.53 ms | 854.4 ms | 838.5 ms | 0.1141× | 0.1163× | 1.0190× |
| `scimark_sparse_mat_mult` | 3.144 ms | 17.44 ms | 17.07 ms | 0.1803× | 0.1842× | 1.0217× |
| `shortest_path` | 425.8 ms | 1056 ms | 1170 ms | 0.4031× | 0.3640× | 0.9030× |
| `spectral_norm` | 75.79 ms | 517.3 ms | 523.1 ms | 0.1465× | 0.1449× | 0.9890× |
| `sqlalchemy_declarative` | 89.23 ms | 1326 ms | 1393 ms | 0.0673× | 0.0641× | 0.9524× |
| `sqlalchemy_imperative` | 11.24 ms | 226.5 ms | 234.3 ms | 0.0496× | 0.0480× | 0.9668× |
| `sqlglot_v2_normalize` | 90.41 ms | 1475 ms | 1536 ms | 0.0613× | 0.0589× | 0.9605× |
| `sqlglot_v2_optimize` | 42.47 ms | 676.9 ms | 694.9 ms | 0.0627× | 0.0611× | 0.9741× |
| `sqlglot_v2_parse` | 988.3 µs | 18.03 ms | 18.97 ms | 0.0548× | 0.0521× | 0.9504× |
| `sqlglot_v2_transpile` | 1.226 ms | 21.92 ms | 22.8 ms | 0.0559× | 0.0538× | 0.9614× |
| `sqlite_synth` | 1.833 µs | 6.1 µs | 6.215 µs | 0.3005× | 0.2949× | 0.9815× |
| `subparsers` | 7.881 ms | 131.7 ms | 133.3 ms | 0.0598× | 0.0591× | 0.9877× |
| `telco` | 5.71 ms | 41.32 ms | 43.14 ms | 0.1382× | 0.1324× | 0.9579× |
| `tornado_http` | 140.1 ms | 1460 ms | 1511 ms | 0.0960× | 0.0927× | 0.9665× |
| `typing_runtime_protocols` | 128.1 µs | 2.834 ms | 2.78 ms | 0.0452× | 0.0461× | 1.0194× |
| `unpack_sequence` | 36.58 ns | 158.2 ns | 162.2 ns | 0.2312× | 0.2255× | 0.9756× |
| `unpickle` | 9.832 µs | 64.02 µs | 65.06 µs | 0.1536× | 0.1511× | 0.9840× |
| `unpickle_list` | 3.215 µs | 11.32 µs | 11.16 µs | 0.2841× | 0.2881× | 1.0140× |
| `unpickle_pure_python` | 168.3 µs | 2.239 ms | 2.259 ms | 0.0752× | 0.0745× | 0.9911× |
