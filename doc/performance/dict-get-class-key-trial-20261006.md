# Exact class-key `dict.get` shortcut trial (2026-10-06)

## Result

Rejected. Bypassing the generic `dict.get` call path for exact built-in class
keys slowed every comparable case in this fast-mode screen. `pyperf
compare_to` reports a **1.17× slowdown** across the reported group; retain the
existing integer and string shortcut only. No source change from this trial
remains.

| Benchmark | Control | Candidate | Candidate / control |
|---|---:|---:|---:|
| `deepcopy` | 2.47 ms ± 0.10 ms | 2.94 ms ± 0.08 ms | 1.19× slower |
| `deepcopy_reduce` | 27.3 μs ± 1.6 μs | 35.5 μs ± 0.9 μs | 1.30× slower |
| `deepcopy_memo` | 263 μs ± 12 μs | 267 μs ± 3 μs | Not significant |
| `pickle_pure_python` | 5.22 ms ± 0.46 ms | 6.15 ms ± 0.63 ms | 1.18× slower |

```text
Relative elapsed time; lower is faster (each bar extends left to right)
deepcopy control     2.47 ms  |####################
deepcopy candidate   2.94 ms  |########################
reduce control      27.3 μs  |###################
reduce candidate    35.5 μs  |#########################
pickle control       5.22 ms  |#####################
pickle candidate     6.15 ms  |#########################
```

The harness warned that all four fast-mode samples were unstable or had high
variance. The slowdown is consistent across the three significant cases, but
the sample size is screening evidence, not a rigorous estimate. The candidate
also remained far behind CPython 3.14.7: the matched candidate run was 2.94 ms
for `deepcopy` versus 211 μs on CPython, and 6.15 ms for pure-Python pickle
versus 251 μs. The fixed full-suite XLang3 control remains the authoritative
suite comparison.

## Hypothesis and decision

CPython's pure-Python `copy` and `pickle` dispatch tables frequently call
`dict.get` with built-in class keys. The VM already has a direct exact-dict
shortcut for integer and string keys. I extended it to class objects whose
metaclass is exactly the built-in `type`, since their hash and equality cannot
run Python code. The intended saving was to avoid method adaptation on these
dispatch lookups while keeping custom metaclasses and observable call hooks on
the normal path.

The benchmark result rejects that implementation: sending those class-key
lookups directly through generic `mapping_get_item` costs more than the
existing built-in method path. The pure-Python `copy.py` and `pickle.py` were
not changed or reimplemented in C++.

A focused XLang3 probe passed built-in class-key hits and misses, defaults,
custom-metaclass equality, and a `copy.deepcopy` graph. It ran against the
candidate Release build in `build-repro/tuple2-candidate`; the fixed
`build-repro/Release` executable was not modified.

## Reproduction data

- [Control pyperf JSON](data/pyperformance-copy-pickle-classkey-control-20261006.json)
- [Candidate pyperf JSON](data/pyperformance-copy-pickle-classkey-candidate-20261006.json)
- [Verbose pyperf comparison](data/pyperformance-copy-pickle-classkey-compare-20261006.txt)
- Runner: `benchmarks/diagnostics/run_pyperformance_xlang3_shimmed.py`
- Python harness: CPython 3.14.7; pyperformance 1.14.0; Windows x64; `--fast`.
