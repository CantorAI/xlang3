# Rejected `BytesIO.write` allocation optimization (2026-10-02)

## Final decision

I removed the append and immutable-bytes view fast paths from
`src/runtime/modules/system/io_module.cpp`. Both paths improved isolated
`BytesIO.write` microbenchmarks, but the measured effect did not carry through
to `pickle_pure_python`, so they were not retained as a pyperformance
optimization. The pure-Python `pickle.py` was never changed.

The implementation boundary was valid: CPython provides native `_io.BytesIO`,
so XLang3 could optimize its own native `_io` implementation while leaving the
pure-Python `pickle` module unchanged. The missing requirement was a material
end-to-end benchmark gain.

## Isolated microbench evidence

The first append-only trial measured **1.028×** speedup over the saved control
(24 order-randomized pairs; paired bootstrap 95% interval **1.022×–1.034×**).
A follow-up that appended immutable `bytes` directly from their in-object view
measured **1.072×** over the append-only build (24 pairs; 95% interval
**1.057×–1.078×**). The workload performed 20 rounds of 10,420 small writes per
sample and verified the final byte string.

The raw pair samples and executable/runtime hashes remain in
[`append microbench data`](data/pickle-bytesio-append-microbench-20261002.json)
and [`bytes-view microbench data`](data/pickle-bytesio-bytes-view-microbench-20261002.json).
The reproducible workload and pair runners remain under
[`scratch/performance`](../../scratch/performance/pickle-bytesio-append-microbench-20261002.py).

## End-to-end pyperf check

The configured Python 3.14 executable is still inaccessible, so I used the
installed pyperf **2.10.0** worker to run the unrolled `pickle_pure_python`
benchmark body and data from the pinned pyperformance 1.14.0 `bm_pickle`
source. This is a focused pyperf run, not the full pyperformance CLI. XLang3
used the available Python 3.13 standard library and compatibility overlay.

With four processes, eight values, three warmups, and a 0.1-second minimum
sample, the saved pre-change control measured **5.33 ± 0.06 ms** and the
candidate measured **5.32 ± 0.09 ms**. `pyperf compare_to` reports no
significant difference. The earlier fast-mode runs were likewise not
significant (**5.31 ± 0.10 ms** control, **5.34 ± 0.08 ms** candidate).

Against the saved CPython 3.14.7 full-run result of **274 µs**, the direct
candidate result is about **19.45× slower**. This cross-run ratio is directional:
the CPython reference is from the full-suite invocation, while this rerun uses
a focused pyperf wrapper.

Raw focused pyperf files are [`control`](data/pickle-pure-pyperf-direct-control-samples-20261002.json),
[`candidate`](data/pickle-pure-pyperf-direct-candidate-samples-20261002.json),
[`fast control`](data/pickle-pure-pyperf-direct-control-fast-20261002.json),
and [`fast candidate`](data/pickle-pure-pyperf-direct-candidate-fast-20261002.json).
The local wrapper is
[`pyperformance-1.14.0-bm_pickle-direct.py`](../../scratch/performance/pyperformance-1.14.0-bm_pickle-direct.py).
The benchmark source is [pyperformance 1.14.0 `bm_pickle/run_benchmark.py`](https://github.com/python/pyperformance/blob/1.14.0/pyperformance/data-files/benchmarks/bm_pickle/run_benchmark.py).

## Fixed gate and remaining status

The candidate passed all 11 cases in the fixed Release regression gate; the
paired report is [`retained-write gate data`](data/pickle-bytesio-retained-write-fixed-baseline-20261002.json).
That gate confirms no broad regression but does not override the lack of a
significant gain in the targeted pyperf case. The write-path code and its
candidate-only fixture were removed; all microbench and pyperf evidence stays
for future comparison.

The goal remains open: the saved full pyperformance run completed 40 of 97
benchmarks and showed only three wins over CPython 3.14.7. The larger remaining
gaps include `telco`, `subparsers`, `pickle_pure_python`, and
`unpickle_pure_python`. A fresh full-suite run still needs an accessible Python
3.14 environment and pyperformance installation.
