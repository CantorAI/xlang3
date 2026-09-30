# Fast-call adapter for native `BytesIO.read`

The XLang3 native `_io.BytesIO.read` method now accepts the VM's positional
arguments through the same stack-backed fast-call adapter already used for
`BytesIO.write` and `BytesIO.tell`. The existing `stream_read` implementation
still owns the method behavior. Text and buffered streams keep their generic
forwarding calls, since those can enter user-provided stream code. The
pure-Python `pickle.py` implementation is unchanged.

The trigger was the official pure-Python unpickler profile: it calls
`_Unframer.read` about 20,200 times, each of which reaches XLang3's own native
`_io.BytesIO.read`. Those reads previously went through the generic native
callback path, which materializes positional arguments and releases/reacquires
the VM execution lock. `BytesIO.read` is implemented natively by CPython's
`_io` module, so this is an XLang3 native-module fast path under the existing
same-name API; it does not move any pure-Python library code into C++.

## Official benchmark results

The benchmark body is pyperformance 1.14.0's unmodified
`bm_pickle/run_benchmark.py`, using pyperf 2.10.0 with
`--pure-python --protocol 5 unpickle`. CPython 3.14.7 ran the pyperf manager;
the XLang3 runtime was selected as the worker executable. This keeps Windows
pipe handling in CPython while executing every measured workload in XLang3.
The XLang3 scores use 40 worker processes, 3 values per worker, and 120 values
per score. The two A/B pairs ran in opposite order and used the same 2 outer
loops × 20 inner loops per value.

| Pair | Runtime | Mean ± standard deviation | Result |
| --- | --- | ---: | --- |
| Parent first | Parent | 4.09 ±0.08 ms | — |
| Parent first | Fast-call adapter | 3.62 ±0.09 ms | **1.13× faster**, significant (`t=41.24`) |
| Candidate first | Parent | 4.17 ±0.14 ms | — |
| Candidate first | Fast-call adapter | 3.65 ±0.09 ms | **1.14× faster**, significant (`t=34.83`) |

Both pyperf pairs emitted the host-jitter warning that their samples did not
meet pyperf's under-1% stability target. The gain remained significant and
similar when the run order was reversed.

```text
unpickle_pure_python elapsed time (shorter is faster; one block ≈ 0.18 ms)
CPython 3.14.7   0.172 ms |█
XLang3 parent    4.09  ms |███████████████████████
XLang3 adapter  3.62  ms |████████████████████
```

Against the fresh CPython 3.14.7 reference, the adapter measures 3.62 ms
versus 0.172 ms: XLang3 is **21.06× slower**, or **0.047× CPython's speed**.
The parent was 23.74× slower. This change improves the case by about 11.5% in
elapsed time but leaves the larger interpreter gap open.

The official `pyperformance run` wrapper could not provision a new XLang3
benchmark environment because its pip bootstrap tried to build an incompatible
`psutil` wheel. The preserved data below comes from the unchanged official
benchmark script and pyperf's rigorous runner, not a synthetic workload.

## Validation and identities

The complete Python fixture runner passed. The complete fixed Release
regression gate also passed with exit 0; it ran all 11 configured cases against
the preserved baseline.

| Artifact | SHA-256 |
| --- | --- |
| `build/Release/xlang3.exe` | `87677A90EF64095E11855208D9DECF108151BE6C0371F4A1DB92E7E7459F6EA7` |
| Parent `xlang3_runtime.dll` | `86F84C5851E018EC95DA85185580B28556F6378852AE36608C55E4129E20E98D` |
| Candidate `xlang3_runtime.dll` | `BB34233BF525634F47321E41D6A4616B17FAF8EA7AC7299FCEB3E7A9B436C965` |

Raw pyperf results and logs:

- [Parent, first order](data/unpickle-bytesio-read-parent-rigorous-20260930.json) and [candidate, first order](data/unpickle-bytesio-read-candidate-rigorous-20260930.json)
- [Candidate, reverse order](data/unpickle-bytesio-read-candidate-repeat-rigorous-20260930.json) and [parent, reverse order](data/unpickle-bytesio-read-parent-repeat-rigorous-20260930.json)
- [Fresh CPython 3.14.7 reference](data/unpickle-bytesio-read-cpython314-rigorous-20260930.json)
- [First-order parent log](data/unpickle-bytesio-read-parent-rigorous-20260930.log), [first-order candidate log](data/unpickle-bytesio-read-candidate-rigorous-20260930.log), [reverse candidate log](data/unpickle-bytesio-read-candidate-repeat-rigorous-20260930.log), [reverse parent log](data/unpickle-bytesio-read-parent-repeat-rigorous-20260930.log), and [CPython log](data/unpickle-bytesio-read-cpython314-rigorous-20260930.log)
