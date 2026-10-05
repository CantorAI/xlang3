# `telco`: exact-type native dispatch trial (2026-10-02)

This trial reduces repeated Python dispatch in pyperformance's `telco` loop.
The VM now calls the existing `_decimal` native add/multiply callback directly
for an exact, unmodified `Decimal` class. `print(file=...)` also writes
directly to an exact built-in `StringIO` when its `write` descriptor is still
the original native method. The normal Python fallback remains in charge for
subclasses, overridden methods, instance shadows, custom streams, and values
the Decimal accelerator cannot handle. `decimal.py` and `_pydecimal.py` remain
Python modules.

The callback and StringIO guards are documented in
[`decimal_module.cpp`](../../src/runtime/modules/system/decimal_module.cpp),
[`xlang_vm_ops_arithmetic.h`](../../src/executor/xlang_vm/ops/xlang_vm_ops_arithmetic.h),
and [`io_module.cpp`](../../src/runtime/modules/system/io_module.cpp). Focused
fixtures cover monkey-patched Decimal operators, StringIO newline translation,
and an instance-shadowed `write` method:
[`decimal_native_arithmetic.py`](../../tests/fixtures/core/decimal_native_arithmetic.py),
[`print_stringio_dispatch.py`](../../tests/fixtures/core/print_stringio_dispatch.py).
Both focused fixtures pass.

The official `pyperformance` 1.14.0 `--fast` candidate result for `telco` was
**268 ms ± 9 ms**. The earlier native-Decimal Release result was **286 ms**;
`pyperf compare_to` reports the candidate **1.07× faster** than that saved run.
The immediately preceding Decimal-dispatch-only result was 270 ms ± 17 ms and
was not significantly different from the combined candidate. Against the
saved CPython 3.14.7 reference (5.98 ms), the candidate is still **44.77×
slower**. This is a measured improvement, but far short of the overall goal.

An opt-in native profile explains where the change acts. One benchmark body
went from 44,073 allocated `BoundMethod` objects before the trial to 21,575
after both shortcuts; the final profile still recorded 12,507 native
quantize calls and 5,001 `print` calls. The raw profile is
[`telco-stringio-profile-20261002.txt`](data/telco-stringio-profile-20261002.txt).
The targeted official pyperf JSON files are
[`candidate`](data/telco-stringio-write-fast-20261002.json),
[`Decimal-only intermediate`](data/telco-bound-method-bypass-fast-20261002.json),
[`previous native-Decimal run`](data/telco-decimal-zero-fast-release-fast-20261002.json),
and [`CPython 3.14.7 reference`](data/telco-decimal-cpython-fast-20261002.json).

The complete 11-case fixed Release gate passed against the preserved
`baseline-0336992` pair, with 21 order-balanced samples per case and a 10%
slowdown threshold. Candidate/baseline ratios were: `local_slots` 0.375×,
`scalar_arithmetic` 0.980×, `range_for` 1.002×, `function_calls` 0.945×,
`class_construct` 0.996×, `list_append` 1.011×, `property_access` 0.996×,
`deepcopy_memo` 0.559×, `json_dumps` 0.838×, `gc_traversal` 0.995×, and
`subparsers` 0.864×. Full paired samples and executable/runtime hashes are in
[`telco-stringio-fixed-release-gate-20261002.json`](data/telco-stringio-fixed-release-gate-20261002.json).

The official comparison still loads XLang3's accessible Python 3.13 standard
library while the reference uses Python 3.14.7's standard library. All 97
pyperformance definitions have not yet been rerun against this candidate, so
the earlier full-suite failure count and geometric mean remain the latest
full-suite results.

## Rejected follow-up

A direct native-call shortcut for exact `Context.quantize` passed an added
`Context.quantize` class-monkeypatch fixture, but measured **275 ms ± 6 ms**
versus **268 ms ± 9 ms** for the accepted candidate. `pyperf compare_to`
reported a significant **1.03× slowdown**, so that shortcut was reverted.
Its raw result remains in
[`telco-context-quantize-fast-20261002.json`](data/telco-context-quantize-fast-20261002.json).
