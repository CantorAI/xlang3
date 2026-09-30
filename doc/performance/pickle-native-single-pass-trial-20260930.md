# Single-pass native `_pickle` writer (2026-09-30)

## Scope and design

The earlier native protocol 4/5 shortcut first traversed a built-in object
graph to decide whether it could handle the graph, then traversed it again to
emit pickle opcodes. This follow-up moves the eligibility checks into the
writer. A single map now tracks active objects for cycle detection and records
memo indices after each object has been emitted. Repeated references therefore
still produce `BINGET`/`LONG_BINGET`, while immediate scalar values skip graph
tracking entirely.

The implementation boundary is deliberate: this C++ path belongs only to
XLang3's native `_pickle` counterpart, which mirrors CPython's native
accelerator and keeps the `_pickle` import name and Python-visible API.
CPython's pure-Python `pickle.py` remains Python code. XLang3's unchanged
`pickle.py` fallback handles cycles, custom objects, deep graphs, and other
objects outside the exact built-in set. The native shortcut admits only
built-in strings, bytes, lists, tuples, dictionaries, immediate scalars, and
the exact standard-library `datetime.date` type. It never invokes an arbitrary
user reducer. If a cycle or unsupported object appears after streaming starts,
the private buffer is discarded and the entire value is passed to the Python
fallback.

The fixture adds a wide dictionary containing both a date and an object with a
side-effecting reducer. It verifies the date round-trip and that the fallback
invokes that reducer exactly once. Existing aliasing, cycle, protocol, and
custom-pickler coverage also passes.

## Official benchmark results

These are protocol 5 `pickle`, `pickle_dict`, and `pickle_list` runs from
pyperformance 1.14.0's unmodified `bm_pickle/run_benchmark.py`, with pyperf
2.10.0 `--rigorous` on Windows, using the same XLang3 3.14.7 executable and
alternating only the runtime DLL for the XLang3 parent and candidate. CPython
3.14.7 used the same official benchmark. Pyperf warned that each individual
run had over 1% sample variation; `pyperf compare_to -v` nevertheless found
the before/after comparisons significant. The practical changes are small and
are reported as such.

| Benchmark | XLang3 parent | XLang3 candidate | Change vs parent | CPython 3.14.7 | XLang3 throughput vs CPython |
| --- | ---: | ---: | ---: | ---: | ---: |
| `pickle` | 18.3 ±1.2 μs | 17.6 ±1.0 μs | **1.04× faster** (`t=5.03`) | 9.13 ±0.14 μs | **0.52×** (1.93× slower; significant) |
| `pickle_dict` | 24.8 ±0.6 μs | 24.1 ±1.0 μs | **1.03× faster** (`t=6.55`) | 23.6 ±0.4 μs | **0.98×** (1.02× slower; significant) |
| `pickle_list` | 4.02 ±0.06 μs | 3.89 ±0.08 μs | **1.03× faster** (`t=14.32`) | 3.96 ±0.07 μs | **1.02×** (about 2% faster; significant) |

The bars show elapsed time relative to the XLang3 parent in each row; less is
faster:

```text
pickle       parent    18.3 μs  ████████████████████
             candidate 17.6 μs  ███████████████████
             CPython    9.13 μs ██████████

pickle_dict  parent    24.8 μs  ████████████████████
             candidate 24.1 μs  ███████████████████
             CPython   23.6 μs  ███████████████████

pickle_list  parent     4.02 μs ████████████████████
             candidate  3.89 μs ███████████████████
             CPython    3.96 μs ████████████████████
```

This removes a measurable amount of work from the native accelerator, but it
does not make the general `pickle` benchmark faster than CPython. The remaining
gap there is about 1.93×, and `pickle_dict` remains about 2% slower. This result
is an incremental native-module gain, not evidence that XLang3 overall beats
CPython.

## Validation and raw data

The complete Python fixture suite passed. The fixed Release regression gate
passed all 11 cases against the preserved accepted executable. The gate report
is retained with these results. The measured XLang3 executable SHA-256 was
`1551FD3479BADE99AFCCC047272C4E1934150A35586F49ACB13C1215868C8794`; the
candidate runtime DLL was
`5A4A9A236C7426E1D2BD5430C01DB97487B2E82D33719AC2D9A2A9A28C994CC6`; and
the parent runtime DLL was
`40083BE6152874892BA3191C90E3FB6B5BA4CE26D057D1D3E0CC5722B6B15556`.

Raw benchmark and gate evidence:

- XLang3 parent: [`pickle`](data/pickle-single-pass-control-pickle-rigorous-20260930.json), [`pickle_dict`](data/pickle-single-pass-control-dict-rigorous-20260930.json), [`pickle_list`](data/pickle-single-pass-control-list-rigorous-20260930.json).
- XLang3 candidate: [`pickle`](data/pickle-singlepass-scalars-candidate-pickle-rigorous-20260930.json), [`pickle_dict`](data/pickle-singlepass-scalars-candidate-dict-rigorous-20260930.json), [`pickle_list`](data/pickle-singlepass-scalars-candidate-list-rigorous-20260930.json).
- CPython 3.14.7: [`pickle`](data/pickle-tree-leaves-cpython314-rigorous-20260930.json), [`pickle_dict`](data/pickle-single-pass-cpython-dict-rigorous-20260930.json), [`pickle_list`](data/pickle-single-pass-cpython-list-rigorous-20260930.json).
- Fixed Release gate: [11-case JSON report](data/pickle-singlepass-scalars-fixed-gate-20260930.json).
