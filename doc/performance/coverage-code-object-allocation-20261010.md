# Code-object churn in the original coverage workload

One unchanged `bench_coverage(1)` invocation on the accepted R7b Release
allocated and finally released the following XLang3 objects:

| Object kind | Allocations | Final releases |
| --- | ---: | ---: |
| Code | 1,213,976 | 1,213,976 |
| Frame | 242,847 | 242,847 |

This is about five code objects per frame. Current `frame.f_code` reads always
call `Value::code`, which allocates a new `CodeObject`; the Python coverage
tracer repeatedly reads that property. These sources and counts identify
allocation churn to investigate. They do not establish its CPU share or the
speedup from eliminating it. No allocation leak is shown by this window.

The window includes Coverage construction/start/stop and the original recursive
fibonacci workload. Imports, a `fibonacci(10) == 55` parity check and a disclosed
one-code-object counter calibration occur outside it. CPython 3.14.7 and XLang3
both completed the original call with valid return durations and restored
tracing/profiling. The duration is deliberately not reported or scored. CPython
allocation counts were not measured; the table contains only XLang3 counts.

The R3 diagnostic reads the existing exported runtime counters through a direct
`POINTER(c_uint64)` FFI return and indexed reads. It verifies the counter bank by
an exactly one-object lazy-code allocation, verifies the enabled flag, restores
its prior state, and never resets counters. Source, complete Release, fixed
baseline and protected dirty-file identity checks passed before and after.
Foreign process activity is retained separately; it is not a timing result.

Two earlier diagnostic attempts failed before the XLang3 benchmark body. The
first could not marshal an array as a void pointer. The second revealed that
the current `ctypes.addressof` returns the XLang object header rather than the
data buffer. Its Win32 buffer call corrupted only the diagnostic child, which
terminated; its source/binary identity checks and cleanup passed. The failures
remain failures and contain no XLang3 allocation result. The R3 path uses no
`addressof`, `cast`, address-based construction, buffer calls or memory writes.
Source review also found that casting a scalar `c_void_p` currently selects its
storage address rather than its pointee. These native `_ctypes` compatibility
issues require a separate coherent fix; the pure-Python ctypes library remains
unchanged.

Next, evaluate lazy reuse of code metadata on observed frames while preserving
active/suspended-frame code replacement, metadata, identity and ownership.
Keep this trial separate from the two-argument numeric IR execution trial.
Any claimed performance improvement requires the original coverage benchmark
and the complete fixed regression gate, not this instrumented allocation count.

Evidence: [successful R3 receipt](data/coverage-f-code-allocation-r3-20261010.json),
[original failed attempt](data/coverage-f-code-allocation-20261010.json),
[second failed attempt](data/coverage-f-code-allocation-r2-20261010.json), and
[publication map](data/coverage-f-code-allocation-publication-20261010.json).
