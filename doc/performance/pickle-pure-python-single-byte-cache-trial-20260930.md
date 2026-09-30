# Pure-Python pickle one-byte bytes cache trial (2026-09-30)

## Finding and implementation boundary

The official pure-Python unpickler profile made 20,200 calls to
`pickle.py`'s `read` helper in one direct workload pass. XLang3 created 21,406
`BytesObject`s in the same instrumented pass. I tested a process-lifetime
cache for the 256 immutable one-byte values in the generic `Value::bytes`
constructor. It reduced the instrumented `BytesObject` allocation count to
5,131 (about 76% fewer) while preserving CPython 3.14's observed identity for
one-byte `bytes` values.

This trial did not implement or modify `pickle.py` in C++. The temporary C++
change was in XLang3's generic immutable-bytes runtime primitive; the pure
standard-library implementation remained Python. The full fixture suite and
the fixed Release gate passed with the candidate. The implementation and its
temporary identity fixture were removed after measurement because they did
not improve the two serialization benchmarks together.

## Official measurements

These are protocol 5 `pickle_pure_python` and `unpickle_pure_python` runs from
pyperformance 1.14.0's unmodified `bm_pickle/run_benchmark.py`, measured with
pyperf 2.10.0 `--rigorous` on the same Windows host and XLang3 executable. The
CPython 3.14.7 reference is a fresh run of the same unpickler case.

| Benchmark | XLang3 parent | Temporary candidate | Candidate throughput vs parent | CPython 3.14.7 |
| --- | ---: | ---: | ---: | ---: |
| `pickle_pure_python` | 6.27 ±0.10 ms | 6.31 ±0.09 ms | **0.99×** (0.6% slower; significant, `t=-2.66`) | — |
| `unpickle_pure_python` | 4.32 ±0.08 ms | 4.29 ±0.07 ms | **1.01×** (0.7% faster; significant, `t=2.61`) | 162 ±3 μs |

The unpickler candidate remains **26.57× slower** than CPython 3.14.7. Across
the two XLang3 cases, the gains cancel to an approximately **1.00×** geometric
mean throughput change:

```text
pickle_pure_python    parent     6.27 ms  ████████████████████
                     candidate  6.31 ms  ████████████████████

unpickle_pure_python  parent     4.32 ms  ████████████████████
                     candidate  4.29 ms  ████████████████████
                     CPython    0.162 ms █
```

The profile confirms that fewer result allocations alone are insufficient:
the candidate still made the same 20,200 `pickle.py.read` calls and 3,700
`load_short_binunicode` calls. The byte cache was therefore discarded. Do not
repeat the singleton-cache change as a standalone performance fix; the larger
remaining target is Python call and VM dispatch overhead in the unpickler.

## Validation and raw data

The full fixture suite passed with the candidate, including a temporary check
that `BytesIO.read(1)` and `bytes([value])` shared the same one-byte object.
The complete fixed Release gate passed all 11 cases against the preserved
accepted executable. The trial's XLang3 executable SHA-256 was
`1551FD3479BADE99AFCCC047272C4E1934150A35586F49ACB13C1215868C8794`; the
parent runtime DLL was
`5A4A9A236C7426E1D2BD5430C01DB97487B2E82D33719AC2D9A2A9A28C994CC6`; and
the temporary candidate runtime DLL was
`11B5912A760572DCC7845E9BF2ACE11CC2D56A33EE9713F4A03425C1E33700A1`.

Raw official pyperf runs: [XLang3 parent pickle](data/pickle-singlebyte-control-xlang3-rigorous-20260930.json), [XLang3 candidate pickle](data/pickle-singlebyte-candidate-xlang3-rigorous-20260930.json), [XLang3 parent unpickle](data/unpickle-singlebyte-control-xlang3-rigorous-20260930.json), [XLang3 candidate unpickle](data/unpickle-singlebyte-candidate-xlang3-rigorous-20260930.json), and [CPython 3.14.7 unpickle](data/unpickle-singlebyte-control-cpython314-rigorous-20260930.json). The [fixed Release gate report](data/pickle-singlebyte-cache-fixed-gate-20260930.json) and the [parent](data/unpickle-singlebyte-parent-profile-20260930.txt) and [candidate](data/unpickle-singlebyte-candidate-profile-20260930.txt) allocation/call profiles are also retained.
