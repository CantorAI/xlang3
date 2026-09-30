# Native `_pickle` tree eligibility walk (2026-09-30)

## Change and implementation boundary

`pickle.dumps()` already routes protocol 4/5 built-in graphs through XLang3's
native `_pickle` module when the graph is safe. Before writing, that shortcut
prewalked the graph with separate `active` and `checked` hash sets. Every
string/bytes leaf and every container therefore incurred an active-set node
allocation and removal, followed by a second checked-set node allocation.

The prewalk now returns immediately for string/bytes leaves, which cannot
contain cycles or subgraphs. For lists, tuples, and dictionaries, one map tracks
both traversal states: `Active` detects a cycle and `Supported` accepts a
previously checked shared container. Exact `datetime.date` validation is
unchanged, as is fallback to Python for custom objects. The writer still owns
pickle memoization, so repeated references are emitted with the existing
`MEMOIZE`/`BINGET` behavior.

This change is only in XLang3's `_pickle`, the native accelerator counterpart
that CPython also provides. `pickle.py` and the `pickle_pure_python` benchmark
remain Python implementations; no pure-Python standard-library behavior was
ported to C++.

## Results

The runs used official pyperformance 1.14.0 benchmarks with pyperf 2.10.0 on
the same Windows host. XLang3 control and candidate used the same CLI executable
and differed only in the runtime DLL. The candidate improved all three
accelerated cases significantly according to `pyperf compare_to`.

| Benchmark | XLang3 control | XLang3 candidate | Candidate speedup | CPython 3.14.7 | XLang3 speed vs CPython |
| --- | ---: | ---: | ---: | ---: | ---: |
| `pickle` | 33.2 ±0.9 μs | 18.2 ±1.0 μs | **1.83×** (`t=122.45`) | 9.13 ±0.14 μs | **0.50×** (1.99× slower) |
| `pickle_dict` | 32.8 ±0.5 μs | 29.7 ±0.7 μs | **1.11×** (`t=39.79`) | 23.4 ±0.3 μs | **0.79×** (1.27× slower) |
| `pickle_list` | 6.29 ±0.10 μs | 5.01 ±0.17 μs | **1.26×** (`t=71.53`) | 3.96 ±0.08 μs | **0.79×** (1.27× slower) |

The three-case geometric mean improvement over the XLang3 control is **1.36×**.
These bars compare elapsed time within each row, normalized to that row's
XLang3 control; shorter bars mean less time:

```text
pickle       control   33.2 μs  ████████████████████
             candidate 18.2 μs  ███████████
             CPython    9.13 μs ██████

pickle_dict  control   32.8 μs  ████████████████████
             candidate 29.7 μs  ██████████████████
             CPython   23.4 μs  ██████████████

pickle_list  control    6.29 μs ████████████████████
             candidate  5.01 μs ████████████████
             CPython    3.96 μs █████████████
```

The principal `pickle` case moved from 3.64× CPython time on the control to
1.99× on the candidate. It is now about twice as slow as CPython 3.14.7, so
this closes a substantial part of that native-accelerator gap without
addressing the separate, much larger `pickle_pure_python` VM gap.

## Validation and evidence

The full Python fixture suite passed, including `pickle_module` coverage for
cycles, repeated references, exact dates, custom picklers, and Python fallback.
The fixed Release regression gate passed all 11 cases with 21 order-balanced
pairs and five warmups per case. Its candidate/control medians ranged from
0.004× to 0.726× elapsed time, within the 10% allowed-regression threshold.

The executable SHA-256 was
`39BFF8DBC50760107BC95B3807DE6CD694176A9F23DE596FAD1A50167DFF6399` for both
XLang3 runs. The control runtime DLL SHA-256 was
`30DCB6F1E1A189979446FED9278D786F5D2D2C94E388F2BC397F6ADF84037A02`; the
candidate DLL SHA-256 was
`72E5B87929488692D03E6BB856637D36FD17C218ECF9872C2F4B80AF2010354C`.

Raw pyperf data: [control fast](data/pickle-tree-leaves-control-fast-20260930.json),
[candidate fast](data/pickle-tree-leaves-candidate-fast-20260930.json),
[reverse-order control](data/pickle-tree-leaves-control-repeat-fast-20260930.json),
[reverse-order candidate](data/pickle-tree-leaves-candidate-repeat-fast-20260930.json),
[control rigorous](data/pickle-tree-leaves-control-rigorous-20260930.json),
[candidate rigorous](data/pickle-tree-leaves-candidate-rigorous-20260930.json),
and [CPython 3.14.7 rigorous](data/pickle-tree-leaves-cpython314-rigorous-20260930.json).
The fixed-baseline check is [here](data/pickle-tree-leaves-fixed-baseline-20260930.json).
