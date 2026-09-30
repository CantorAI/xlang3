# Reuse native `_pickle` traversal nodes for memoization (2026-09-30)

## Change and implementation boundary

The protocol 4/5 native `_pickle` fast path first walks built-in list, tuple, and dictionary graphs to confirm that they are safe for the native writer. Before this experiment, that walk allocated a container-state map, then the writer allocated a separate memo map for the same containers. The candidate keeps traversal state, memo index, and memoized status together in `PickleObjectState`. The writer reuses the nodes created by the walk; strings and bytes still avoid prewalk entries and acquire memo entries only when needed during encoding.

This is an implementation of the native `_pickle` accelerator boundary that CPython also exposes. The pure-Python `pickle.py` implementation and the `pickle_pure_python` benchmark remain Python code and were not changed. No CPython implementation is linked or called; XLang3 provides its own native module.

## Results

These official pyperformance 1.14.0 / pyperf 2.10.0 rigorous runs compare the current parent (`034b809`) with the candidate on the same Windows host and executable, swapping only the XLang3 runtime DLL. Lower time is better. `t` is from `pyperf compare_to`; the `pickle` improvement is small despite clearing its significance threshold, while dictionary and list improvements are substantial.

| Benchmark | Control | Candidate | Candidate speedup | CPython 3.14.7 | Candidate speed vs CPython |
| --- | ---: | ---: | ---: | ---: | ---: |
| `pickle` | 18.0 ±0.5 μs | 17.9 ±0.7 μs | 1.01× (`t=2.37`) | 9.13 ±0.14 μs | 0.51× throughput (1.96× slower) |
| `pickle_dict` | 30.1 ±0.7 μs | 24.4 ±0.5 μs | 1.24× (`t=74.55`) | 23.4 ±0.3 μs | 0.96× throughput (1.04× slower) |
| `pickle_list` | 5.08 ±0.20 μs | 4.05 ±0.31 μs | 1.25× (`t=30.45`) | 3.96 ±0.08 μs | 0.98× throughput (1.02× slower) |

The three-case geometric mean improvement over the XLang3 control is **1.16×**. These left-to-right bars show elapsed time, scaled independently within each benchmark row; the control is 20 blocks in every row, and shorter bars mean less time:

```text
pickle       control    18.0 μs  ████████████████████
             candidate  17.9 μs  ████████████████████
             CPython     9.13 μs ██████████

pickle_dict  control    30.1 μs  ████████████████████
             candidate  24.4 μs  ████████████████
             CPython   23.4 μs  ████████████████

pickle_list  control     5.08 μs ████████████████████
             candidate   4.05 μs ████████████████
             CPython     3.96 μs ████████████████
```

This removes most of the remaining gap in `pickle_dict` and `pickle_list`, but the general `pickle` case is still about twice as slow as CPython 3.14.7. The separate pure-Python pickle/unpickle slowdown is unaffected and must be pursued through generic XLang3 VM/runtime work, keeping the pure-Python module intact.

## Validation and reproducibility

The full Python fixture suite passed, including pickle cycles, shared references, dates, custom picklers, and Python fallback. The fixed Release regression gate passed all 11 cases with 21 order-balanced pairs and five warmups; candidate/control elapsed-time ratios ranged from 0.0042649× to 0.7280834×, inside the 10% regression allowance.

The XLang3 executable SHA-256 was `39BFF8DBC50760107BC95B3807DE6CD694176A9F23DE596FAD1A50167DFF6399`. The control DLL (`034b809`) SHA-256 was `72E5B87929488692D03E6BB856637D36FD17C218ECF9872C2F4B80AF2010354C`; the candidate DLL SHA-256 was `FF1BD7C84A24909E417662B6CD5BE7B9E05E6577536FC460842F60141B97D3B2`. The CPython reference is the fresh 3.14.7 run recorded by the preceding native tree-walk trial.

Raw candidate/control measurements: [control fast](data/pickle-combined-state-control-fast-20260930.json), [candidate fast](data/pickle-combined-state-candidate-fast-20260930.json), [candidate reverse-order fast](data/pickle-combined-state-candidate-repeat-fast-20260930.json), [control reverse-order fast](data/pickle-combined-state-control-repeat-fast-20260930.json), [control rigorous](data/pickle-combined-state-control-rigorous-20260930.json), and [candidate rigorous](data/pickle-combined-state-candidate-rigorous-20260930.json). The [fixed Release baseline gate](data/pickle-combined-state-fixed-baseline-20260930.json) preserves its paired measurements. The [CPython 3.14.7 rigorous reference](data/pickle-tree-leaves-cpython314-rigorous-20260930.json) is from the [preceding tree-walk trial](pickle-native-tree-walk-trial-20260930.md).
