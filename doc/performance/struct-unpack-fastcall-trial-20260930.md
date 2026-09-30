# Fast-call adapter for native `_struct.unpack` (2026-09-30)

Registering XLang3's existing native `_struct.unpack` implementation with the
stack-backed positional adapter moved all 540 observed unpickler calls off the
generic native-call path. A longer rigorous run measured a small 1% speedup,
while earlier short runs were noisy and disagreed in direction. The change
passes the complete fixture suite and all 11 fixed Release regression cases.

The pyperformance `pickle.py` implementation remains Python. `_struct` is
already a CPython-native module, so this optimization stays within the native
module boundary and leaves the existing function behavior and import surface
in place. The source comment beside registration records why the hot two-arg
call uses the adapter.

## Profile evidence

One direct diagnostic pass of the unchanged pyperformance 1.14.0
`bm_pickle/run_benchmark.py` with `--pure-python --protocol 5 unpickle`
recorded 540 calls to `_struct.unpack` on the generic slow path. With the
adapter, the same workload's `fast` native-call count rose from 177,958 to
178,498, exactly 540 calls, and `_struct.unpack` no longer appeared among slow
native calls. These counters confirm path selection; they are not timing data.

## Official pyperformance measurements

The benchmark body and pyperf 2.10.0 runner were unchanged. XLang3 worker
executables ran under the CPython 3.14.7 manager. The first two A/B comparisons
used two outer loops per value; a later, longer comparison used ten outer
loops. Each score has 40 worker processes and 120 measured values.

| Run | Parent | Adapter | Result |
| --- | ---: | ---: | --- |
| Short, first order | 3.62 ±0.09 ms | 3.42 ±0.05 ms | 1.06× faster, significant (`t=20.76`) |
| Short, reverse order | 3.59 ±0.15 ms | 3.66 ±0.30 ms | 1.02× slower, significant (`t=-2.58`) |
| Short, pooled | 3.61 ±0.12 ms | 3.54 ±0.24 ms | 1.02× faster, significant (`t=3.45`) |
| Ten loops per value | 3.55 ±0.09 ms | 3.52 ±0.07 ms | 1.01× faster, significant (`t=2.69`) |

The short-order reversal makes clear that individual scores move with host
jitter. Both ten-loop scores also report pyperf's warning that they do not
meet its under-1% stability target. The longer comparison supports a small
gain, not a large one. The pooled short-run JSON normalizes only the worker
executable metadata to a runtime identity; the individual raw files retain
their actual executable paths.

Against the fresh CPython 3.14.7 reference from the preceding
[`BytesIO.read` trial](bytesio-read-fastcall-trial-20260930.md), the longer
run is 3.52 ms versus 0.172 ms: XLang3 remains **20.47× slower**, or **0.049×
CPython's speed**.

```text
unpickle_pure_python elapsed time (shorter is faster; one block ≈ 0.15 ms)
CPython 3.14.7       0.172 ms |█
XLang3 parent        3.55  ms |████████████████████████
XLang3 with adapter  3.52  ms |███████████████████████
```

## Validation and identities

The complete Python fixture runner exited 0. The complete fixed Release
regression gate passed all 11 configured cases with exit 0. The pyperf
candidate and parent used the same `xlang3.exe`; their runtime DLLs were:

| Artifact | SHA-256 |
| --- | --- |
| `xlang3.exe` | `87677A90EF64095E11855208D9DECF108151BE6C0371F4A1DB92E7E7459F6EA7` |
| Parent `xlang3_runtime.dll` | `BB34233BF525634F47321E41D6A4616B17FAF8EA7AC7299FCEB3E7A9B436C965` |
| Adapter `xlang3_runtime.dll` | `13FC7CE53D99F80133AB914813699CD8F5C0A5B8371C82ED5796A15BE7619316` |

Raw short runs: [parent, first order](data/unpickle-bytesio-read-candidate-rigorous-20260930.json),
[adapter, first order](data/unpickle-struct-unpack-candidate-rigorous-20260930.json),
[parent, reverse order](data/unpickle-struct-unpack-parent-repeat-rigorous-20260930.json),
and [adapter, reverse order](data/unpickle-struct-unpack-candidate-repeat-rigorous-20260930.json).
The [pooled parent](data/unpickle-struct-unpack-parent-merged-rigorous-20260930.json)
and [pooled adapter](data/unpickle-struct-unpack-candidate-merged-rigorous-20260930.json)
retain all 80 short-run samples per side. Longer runs are the [parent](data/unpickle-struct-unpack-parent-loops10-rigorous-20260930.json)
and [adapter](data/unpickle-struct-unpack-candidate-loops10-rigorous-20260930.json).
Each score's captured runner output is stored beside its JSON file.
The [pre-change counters](data/unpickle-struct-unpack-parent-counters-20260930.txt),
[post-change counters](data/unpickle-struct-unpack-candidate-counters-20260930.txt),
and [11-case Release gate](data/release-regression-struct-unpack-fixed-baseline-20260930.json)
preserve the diagnostics and validation report.
