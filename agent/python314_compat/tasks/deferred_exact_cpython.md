# Deferred Exact CPython Tasks

- [~] deep Unicode database
  Coverage: `tests/fixtures/compat_sections/strings_and_unicode.py`
  Remaining: generated tables for all Unicode data, normalization, segmentation, casing, and identifier edge cases.

- [~] pyc and frozen bytecode parity
  Coverage: import fixtures.
  Remaining: `.pyc` read/write/execution, invalidation, and CPython frozen-code table behavior.

- [~] native CPython extension ABI simulation
  Coverage: none.
  Remaining: PyObject compatibility layer, NumPy-style extension loading strategy, refcount/type simulation, and risk model.

