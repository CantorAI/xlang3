# CPython 3.14.7 shared-dependency full reference, 2026-10-01

All **97 definitions were attempted**: **92 completed**, **5 failed**, **0 partial definitions**, and **116 raw subtest timings**. This is a reference run; the paired XLang3 run is still in progress. No overall speed comparison is available yet.

This run uses pyperformance 1.14.0 and pyperf 2.10 in fast mode. The shared dependency directory is `venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages`. Both runtimes use the same priority/host-metadata compatibility hooks, a 600-second full-definition deadline, and a 120-second `async_tree*` deadline. Deadlines cover calibration and workers; timed workloads, loop counts, warmups and sample selection are not rewritten.

Fast-mode results include pyperf instability warnings in the raw log. Values below are arithmetic means of raw measurement values, excluding warmups. A benchmark definition can emit several subtests, so the timing count differs from 97.

- [Raw pyperf JSON](data/pyperformance-cpython314-native-iocp-shared-deps-full-fast-20261001.json)
- [Complete log](data/pyperformance-cpython314-native-iocp-shared-deps-full-fast-20261001.log)
- [All 97 statuses](data/pyperformance-cpython314-native-iocp-shared-deps-full-fast-20261001-all-97-status.csv)
- [Subtest timings](data/pyperformance-cpython314-native-iocp-shared-deps-full-fast-20261001-subtests.csv)
- [Paired binary identities and run manifest](data/pyperformance-native-iocp-shared-deps-run-manifest-20261001.json)

## Failures remain part of the reference

`django_template` and `sympy` explicitly fail importing `distutils` from the shared dependency versions. `2to3`, `python_startup`, and `python_startup_no_site` fail in pyperf's command timer with exit 1. Source inspection identifies the original compatibility hook's eager pyperf import as conflicting with the timer's no-pyperf-import guard; command-level confirmation is pending. These three failures do not establish that CPython's timed commands themselves fail.

The XLang3 binary measured by the companion run is from `1924e4b`; the new native `_asyncio` source draft is outside that frozen binary. Do not attribute this reference to the unbuilt candidate.

## Complete definition list

| Definition | Reference status | Subtest means | Failure |
|---|---|---|---|
| 2to3 | failed |  | Benchmark died |
| argparse | completed | many_optionals=0.69555ms |  |
| argparse_subparsers | completed | subparsers=7.89102ms |  |
| async_generators | completed | async_generators=292.685ms |  |
| async_tree | completed | async_tree_none=254.011ms |  |
| async_tree_cpu_io_mixed | completed | async_tree_cpu_io_mixed=457.961ms |  |
| async_tree_cpu_io_mixed_tg | completed | async_tree_cpu_io_mixed_tg=465.358ms |  |
| async_tree_eager | completed | async_tree_eager=88.4818ms |  |
| async_tree_eager_cpu_io_mixed | completed | async_tree_eager_cpu_io_mixed=367.529ms |  |
| async_tree_eager_cpu_io_mixed_tg | completed | async_tree_eager_cpu_io_mixed_tg=400.494ms |  |
| async_tree_eager_io | completed | async_tree_eager_io=600.39ms |  |
| async_tree_eager_io_tg | completed | async_tree_eager_io_tg=574.122ms |  |
| async_tree_eager_memoization | completed | async_tree_eager_memoization=206.156ms |  |
| async_tree_eager_memoization_tg | completed | async_tree_eager_memoization_tg=269.966ms |  |
| async_tree_eager_tg | completed | async_tree_eager_tg=209.916ms |  |
| async_tree_io | completed | async_tree_io=577.756ms |  |
| async_tree_io_tg | completed | async_tree_io_tg=587.953ms |  |
| async_tree_memoization | completed | async_tree_memoization=289.201ms |  |
| async_tree_memoization_tg | completed | async_tree_memoization_tg=305.891ms |  |
| async_tree_tg | completed | async_tree_none_tg=262.104ms |  |
| asyncio_tcp | completed | asyncio_tcp=776.933ms |  |
| asyncio_tcp_ssl | completed | asyncio_tcp_ssl=4379.93ms |  |
| asyncio_websockets | completed | asyncio_websockets=192.493ms |  |
| base64 | completed | ascii85_large=951.981ms; ascii85_small=17.1658ms; base16_large=7.01613ms; base16_small=0.312339ms; base32_large=427.623ms; base32_small=8.02091ms; base64_large=7.99297ms; base64_small=0.300943ms; base85_large=329.289ms; base85_small=5.68644ms; urlsafe_base64_small=0.472185ms |  |
| bpe_tokeniser | completed | bpe_tokeniser=3862.73ms |  |
| chameleon | completed | chameleon=12.2304ms |  |
| chaos | completed | chaos=49.8668ms |  |
| comprehensions | completed | comprehensions=0.0162479ms |  |
| concurrent_imap | completed | bench_mp_pool=170.365ms; bench_thread_pool=1.14598ms |  |
| coroutines | completed | coroutines=18.0127ms |  |
| coverage | completed | coverage=77.2375ms |  |
| crypto_pyaes | completed | crypto_pyaes=58.8473ms |  |
| dask | completed | dask=809.246ms |  |
| deepcopy | completed | deepcopy=0.211783ms; deepcopy_memo=0.0253226ms; deepcopy_reduce=0.0023156ms |  |
| deltablue | completed | deltablue=2.66903ms |  |
| django_template | failed |  | Benchmark died |
| docutils | completed | docutils=2010.25ms |  |
| dulwich_log | completed | dulwich_log=78.5795ms |  |
| fannkuch | completed | fannkuch=329.344ms |  |
| fastapi | completed | fastapi_http=586.125ms |  |
| float | completed | float=58.5452ms |  |
| gc_collect | completed | create_gc_cycles=1.56977ms |  |
| gc_traversal | completed | gc_traversal=2.57397ms |  |
| generators | completed | generators=27.359ms |  |
| genshi | completed | genshi_text=20.1822ms; genshi_xml=45.8623ms |  |
| go | completed | go=102.977ms |  |
| hexiom | completed | hexiom=5.15735ms |  |
| html5lib | completed | html5lib=48.829ms |  |
| json_dumps | completed | json_dumps=8.10012ms |  |
| json_loads | completed | json_loads=0.0190893ms |  |
| logging | completed | logging_format=0.00810965ms; logging_silent=7.23373e-05ms; logging_simple=0.00792523ms |  |
| mako | completed | mako=8.33166ms |  |
| mdp | completed | mdp=1097.79ms |  |
| meteor_contest | completed | meteor_contest=91.8539ms |  |
| nbody | completed | nbody=84.8406ms |  |
| networkx | completed | shortest_path=505.213ms |  |
| networkx_connected_components | completed | connected_components=466.032ms |  |
| networkx_k_core | completed | k_core=2557.23ms |  |
| nqueens | completed | nqueens=80.1894ms |  |
| pathlib | completed | pathlib=53.5477ms |  |
| pickle | completed | pickle=0.00948252ms |  |
| pickle_dict | completed | pickle_dict=0.0243319ms |  |
| pickle_list | completed | pickle_list=0.00428654ms |  |
| pickle_pure_python | completed | pickle_pure_python=0.264637ms |  |
| pidigits | completed | pidigits=172.844ms |  |
| pprint | completed | pprint_pformat=1275.49ms; pprint_safe_repr=650.824ms |  |
| pyflate | completed | pyflate=378.573ms |  |
| python_startup | failed |  | Benchmark died |
| python_startup_no_site | failed |  | Benchmark died |
| raytrace | completed | raytrace=235.267ms |  |
| regex_compile | completed | regex_compile=107.197ms |  |
| regex_dna | completed | regex_dna=139.068ms |  |
| regex_effbot | completed | regex_effbot=1.93984ms |  |
| regex_v8 | completed | regex_v8=17.0298ms |  |
| richards | completed | richards=34.9946ms |  |
| richards_super | completed | richards_super=38.8197ms |  |
| scimark | completed | scimark_fft=248.066ms; scimark_lu=78.8077ms; scimark_monte_carlo=54.3891ms; scimark_sor=101.756ms; scimark_sparse_mat_mult=3.25895ms |  |
| spectral_norm | completed | spectral_norm=78.8132ms |  |
| sphinx | completed | sphinx=805.285ms |  |
| sqlalchemy_declarative | completed | sqlalchemy_declarative=92.55ms |  |
| sqlalchemy_imperative | completed | sqlalchemy_imperative=11.8815ms |  |
| sqlglot_v2 | completed | sqlglot_v2_normalize=90.2243ms |  |
| sqlglot_v2_optimize | completed | sqlglot_v2_optimize=45.3865ms |  |
| sqlglot_v2_parse | completed | sqlglot_v2_parse=1.07984ms |  |
| sqlglot_v2_transpile | completed | sqlglot_v2_transpile=1.26094ms |  |
| sqlite_synth | completed | sqlite_synth=0.00208452ms |  |
| sympy | failed |  | Benchmark died |
| telco | completed | telco=5.82318ms |  |
| tomli_loads | completed | tomli_loads=1887.69ms |  |
| tornado_http | completed | tornado_http=275.399ms |  |
| typing_runtime_protocols | completed | typing_runtime_protocols=0.133945ms |  |
| unpack_sequence | completed | unpack_sequence=3.84155e-05ms |  |
| unpickle | completed | unpickle=0.0119227ms |  |
| unpickle_list | completed | unpickle_list=0.00340315ms |  |
| unpickle_pure_python | completed | unpickle_pure_python=0.174654ms |  |
| xdsl | completed | xdsl_constant_fold=40.5611ms |  |
| xml_etree | completed | xml_etree_generate=75.3642ms; xml_etree_iterparse=74.7978ms; xml_etree_parse=115.495ms; xml_etree_process=50.2546ms |  |
