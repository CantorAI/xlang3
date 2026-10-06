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

## Validation and limits

The isolated Release candidate built in
`build-repro/tuple2-candidate/Release`; the fixed
`build-repro/Release/xlang3.exe` hash remained
`A5F5028C15E145EDCE645A5AFC25C11FBCE77F51E882312B1FBE06E63C72A4AF`.
The candidate passed the existing `core_value_and_object_model` and
`system_stdlib` fixtures, covering scalar values, instances, classes,
metaclasses, and standard-library uses of `type()`.

The full `tests/run_fixtures.py` run stopped at `ssl_timed_socket` because this
XLang3 build could not import `_ssl`. A pyperformance `bm_deepcopy` run was
also attempted, but pyperformance tried to create a new candidate venv and
failed during `ensurepip`: the candidate runtime lacks hashlib algorithms and
`mmap` needed by pip. Therefore this change has direct-body evidence but no
new official pyperf result yet.

The specialization does not implement or replace any Python standard-library
module. It handles one exact builtin operation in the generic VM, and leaves
the original `copy.py` and normal class-call fallback in place. The source
comment describes why the specialization exists and the observability guard
that protects it.
