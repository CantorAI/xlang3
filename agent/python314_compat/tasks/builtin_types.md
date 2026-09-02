# Builtin Type Tasks

- [x] containers
  Coverage: `tests/fixtures/compat_sections/containers.py`
  Remaining: none in the current scoped audit.

- [~] strings and Unicode
  Coverage: `tests/fixtures/compat_sections/strings_and_unicode.py`
  Remaining: generated Unicode database, normalization completeness, locale casing, grapheme segmentation, identifier edge cases, and full codec registry behavior.

- [~] bytes, bytearray, and memoryview
  Coverage: `tests/fixtures/core/binary_buffers.py`, `tests/fixtures/compat_sections/builtins.py`
  Remaining: full buffer protocol parity and memoryview format/cast/release edge cases.

