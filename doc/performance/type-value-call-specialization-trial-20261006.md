# Exact `type(value)` call-site specialization trial (2026-10-06)

## Result

The one-argument call to the exact builtin `type` class now has a guarded
call-site specialization. It uses XLang3's existing runtime type mapping and
falls back to ordinary class-call handling whenever profiling, tracing,
debugging, keywords, argument expansion, or an unsupported value makes the
fast path ineligible. The call cache is keyed by the exact callee object, so a
rebinding to another callable uses normal dispatch.

The motivation was a same-process microbenchmark of `type(1)`: CPython 3.14.7
took a median **0.041 μs/call**, while the fixed XLang3 Release took **0.728
μs/call**. The candidate reduced that to **0.581 μs/call** (about 20%). Other
builtin-call probes (`id`, `len`, and `hash`) did not materially move; this is
a narrow result, not a general builtin-call speed claim.

The unchanged pyperformance `bm_deepcopy` body, run for 30 loops seven times
per runtime in alternating CPython/control/candidate order, produced these
medians using the benchmark-reported body time:

| Runtime | Median body time | Relative to CPython |
|---|---:|---:|
| CPython 3.14.7 | 6.172 ms | 1.00× |
| XLang3 fixed Release control | 76.983 ms | 12.48× slower |
| XLang3 `type(value)` candidate | 71.995 ms | 11.67× slower |

The candidate is **6.48% faster** than the control on this direct-body probe.
This remains far slower than CPython, and the short direct-body probe is not an
official pyperf score. Raw samples are in
[`type-value-call-specialization-20261006.json`](data/type-value-call-specialization-20261006.json).
The supporting call, atomic-path, shape, and `_keep_alive` probes are
[`call_builtin_microbench.py`](../../benchmarks/diagnostics/call_builtin_microbench.py),
[`deepcopy_atomic_path_microbench.py`](../../benchmarks/diagnostics/deepcopy_atomic_path_microbench.py),
[`deepcopy_shape_microbench.py`](../../benchmarks/diagnostics/deepcopy_shape_microbench.py),
and [`deepcopy_keep_alive_branches.py`](../../benchmarks/diagnostics/deepcopy_keep_alive_branches.py).

The official pyperformance 1.14.0 `deepcopy` group has since completed in
`--fast` mode through XLang3's existing Windows-compatible runner. A matched
CPython 3.14.7 run used that same runner and dependency site:

| Case | CPython 3.14.7 | Fixed Release run | Candidate | Candidate slowdown vs CPython |
|---|---:|---:|---:|---:|
| `deepcopy` | 211 μs | 2.70 ms | 2.51 ms | 11.87× |
| `deepcopy_reduce` | 2.20 μs | 28.6 μs | 27.2 μs | 12.37× |
| `deepcopy_memo` | 22.0 μs | 297 μs | 264 μs | 11.98× |

On these three cases, `pyperf compare_to` moved the geometric-mean slowdown
from **13.08×** on the fixed Release run to **12.07×** on the candidate, about
**8.4% faster**. The fast-mode samples carry pyperf stability warnings. The
fixed Release column comes from the complete same-day suite run; a fresh
control rerun did not get past its worker's `psutil` priority hook. The
candidate and current CPython runs completed. The full 97-case suite has not
been rerun on this candidate, so this gain should not be applied to the
whole-suite ratio yet. Raw files are
[`pyperformance-deepcopy-typecache-candidate-20261006.json`](data/pyperformance-deepcopy-typecache-candidate-20261006.json),
[`pyperformance-deepcopy-cpython314-current-20261006.json`](data/pyperformance-deepcopy-cpython314-current-20261006.json),
and the saved [fixed Release full-suite data](data/pyperformance-xlang3-main-post-asyncio-thread-state-full-fast-20261006.json).

## Validation and limits

The isolated Release candidate built in
`build-repro/tuple2-candidate/Release`; the fixed
`build-repro/Release/xlang3.exe` hash remained
`A5F5028C15E145EDCE645A5AFC25C11FBCE77F51E882312B1FBE06E63C72A4AF`.
The candidate passed the existing `core_value_and_object_model` and
`system_stdlib` fixtures, covering scalar values, instances, classes,
metaclasses, and standard-library uses of `type()`.

The full `tests/run_fixtures.py` run stopped at `ssl_timed_socket` because this
XLang3 build could not import `_ssl`. The ordinary pyperformance CLI also
cannot bootstrap an XLang3 venv because the runtime lacks hashlib algorithms
and `mmap` needed by pip. The existing XLang3 pyperformance harness avoids
that setup and successfully ran this benchmark group against the already
installed Python 3.14 dependencies.

The specialization does not implement or replace any Python standard-library
module. It handles one exact builtin operation in the generic VM, and leaves
the original `copy.py` and normal class-call fallback in place. The source
comment describes why the specialization exists and the observability guard
that protects it.
