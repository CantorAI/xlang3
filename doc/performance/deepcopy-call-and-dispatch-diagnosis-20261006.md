# Deepcopy call and dispatch diagnosis (2026-10-06)

## Finding

The official `deepcopy` family is consistently about 11–12× slower in the
current XLang3 Release runtime than in CPython 3.14.7, even though both
runtimes execute the same pure-Python `copy.py` from
`C:\Python\Python314\Lib\copy.py`. The gap is not explained by one missing
tuple-unpack path or by an avoidable retain on ordinary local loads: XLang3
already has those fast paths. Small isolated loops show a roughly 1.4× integer
loop gap and a roughly 3× gap for simple Python calls and dict access, which
does not by itself explain the larger object-graph workload gap.

This report records a diagnosis, not a claimed speedup. No source optimization
from this pass is retained.

## Same-source measurements

The measurements used CPython **3.14.7** at `C:\Python\Python314`, XLang3's
fixed `build-repro\Release\xlang3.exe`, and pyperformance 1.14.0's unchanged
`bm_deepcopy/run_benchmark.py`. The direct-body helper bypassed the pyperf
worker while preserving the official benchmark function. Three same-process
samples were used for these diagnostic comparisons; the full official suite
result remains the source for suite-level claims.

| Workload | CPython 3.14.7 | XLang3 Release | XLang3 slowdown |
|---|---:|---:|---:|
| `benchmark`, 30 loops | 6.35 ms | 76.74 ms | 12.1× |
| `benchmark_reduce`, 100 loops | 0.238 ms | 2.77 ms | 11.6× |
| `benchmark_memo`, 2 loops | 0.053 ms | 0.606 ms | 11.4× |
| Official `benchmark`, 300 loops | 64.7 ms | 0.80 s median | 12.4× |

The component probe repeatedly called the unchanged standard-library helpers
on the same small exact-built-in inputs:

| Helper | Calls | CPython 3.14.7 | XLang3 Release | XLang3 slowdown |
|---|---:|---:|---:|---:|
| `copy._keep_alive` | 5,000 | 0.535 ms | 5.09 ms | 9.5× |
| `copy._deepcopy_dict` | 5,000 | 1.57 ms | 19.88 ms | 12.6× |
| `copy._deepcopy_list` | 3,000 | 1.19 ms | 18.52 ms | 15.6× |
| `copy.deepcopy` | 3,000 | 2.22 ms | 28.09 ms | 12.7× |

```text
Relative elapsed time; shorter is faster (bars extend left to right)
CPython baseline       1.0× |██
plain integer loop     1.4× |███
plain Python calls     3.0× |██████
dict read/write loop   3.4× |███████
deepcopy family       11–12× |████████████████████████
```

The simple loops are diagnostic probes, not pyperformance cases. Their source
is in [`vm_hotpath_microbench.py`](../../benchmarks/diagnostics/vm_hotpath_microbench.py)
and [`deepcopy_parts.py`](../../benchmarks/diagnostics/deepcopy_parts.py);
[the helper probe](../../benchmarks/diagnostics/deepcopy_helpers.py) isolates
the pure-Python helper functions;
[`run_deepcopy_body.py`](../../benchmarks/diagnostics/run_deepcopy_body.py)
runs the unchanged pyperformance body directly. These short samples locate the
scale of the gap but are not a stable ranking of individual opcodes.

## What the source and VM already do

XLang3 compiles CPython 3.14.7's `copy.py`; it does not replace the pure-Python
library with C++. Its `deepcopy` function lowers to 106 IR instructions in
[`copy.ir.txt`](data/copy-ir-xlang3-20261006/copy.ir.txt). The same process's
warmed CPython disassembly is in
[`deepcopy-cpython314-warmed-bytecode-20261006.txt`](data/deepcopy-cpython314-warmed-bytecode-20261006.txt).
Operation counts alone do not explain the result: XLang3's IR has fewer
instructions than the 156 decoded warmed CPython operations, while each VM
operation still has to implement XLang3's dynamic runtime semantics.

The hot `_deepcopy_dict` loop obtains `x.items()`, iterates, unpacks each
two-item tuple, recursively calls `deepcopy` for both entries, then assigns
into the result dict. XLang3 directly unpacks exact tuples/lists and combines
the following `StoreLocalPair` when execution is unobservable, matching the
shape of CPython 3.14's `UNPACK_SEQUENCE_TWO_TUPLE` plus paired local store.
`LoadLocal` and `LoadLocalPair` also borrow object references while the frame
owns the locals. The `memo.get(id(x), _nil)` path already has an exact-dict
fast path for integer keys. Retrying any of these same narrow optimizations
would repeat existing work.

A separate candidate removed the temporary vector used when constructing
two-item tuples from dict iteration and avoided copying the entry pair first.
Five alternating 300-loop direct-body runs overlapped the fixed Release
control: the median was about 0.81 s on each. The change was removed. It is
not a useful speedup to carry forward.

The Python-call microbenchmark was about 3× slower in XLang3, while the full
deepcopy body was about 12× slower. That difference points to costs accumulated
across object-heavy dynamic operations and frame transitions, rather than a
single small allocation or one call-site cache miss. The diagnostic Python
call profile records equal high-level call counts for both runtimes, but its
`sys.setprofile` timings distort normal execution and are not evidence for
per-function wall time ([CPython profile](data/deepcopy-python-call-profile-cpython314-20261006.txt),
[XLang3 profile](data/deepcopy-python-call-profile-xlang3-20261006.txt)). Native
sampling and VM counters ([opcode counters](data/deepcopy-vm-counters-xlang3-20261006.txt),
[second symbolized sample](data/deepcopy-native-samples-xlang3-relwithdebinfo-r2-20261006.json))
remain supporting clues only; they have not yet isolated a single dominant
uninstrumented operation.

## Next optimization target

Continue with a low-overhead, uninstrumented attribution of object-heavy
operations in the same deepcopy body, then change a shared runtime/VM path only
if its cost is both repeatable and large enough to explain a meaningful share
of the 11–12× gap. The optimized body must still execute the installed Python
`copy.py`; any native work belongs in XLang3's generic runtime or in a native
module counterpart to a CPython-native module. Require a matched 3.14.7
pyperformance comparison and correctness coverage before retaining a change.

The official full-suite status is recorded in
[`pyperformance-xlang3-main-post-asyncio-thread-state-full-fast-20261006.md`](pyperformance-xlang3-main-post-asyncio-thread-state-full-fast-20261006.md).

The follow-up exact class-key `dict.get` shortcut was rejected after it
regressed `deepcopy`, `deepcopy_reduce`, and `pickle_pure_python`; see the
[matched trial data](dict-get-class-key-trial-20261006.md). Continue profiling
other object-heavy runtime operations instead of retrying generic dispatch
shortcuts on these same paths.

The follow-up four-entry `type(value)` result cache also failed to improve
deepcopy or pickle; its extra per-site state was removed. See the
[type-result cache trial](type-value-polymorphic-result-cache-trial-20261006.md)
before considering another type-result cache.
