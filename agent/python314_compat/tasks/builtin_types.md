# Builtin Type Tasks

- [x] containers
  Coverage: `tests/fixtures/compat_sections/containers.py`
  Remaining: none in the current scoped audit.

- [x] strings and Unicode
  Coverage: `tests/fixtures/compat_sections/strings_and_unicode.py`,
  `tests/fixtures/core/builtin_types_edges.py`
  Completed: code-point string storage/indexing, Unicode classification and
  representative full casing/casefold mappings, identifiers, normalization,
  and the supported codec paths are CPython-differential covered. Python string
  casing is locale-independent, and grapheme segmentation is not a `str` API.
  Exhaustive Unicode 16.0/3.2.0 database and codec-catalog parity is covered in
  `deferred_exact_cpython.md`.

- [x] bytes, bytearray, and memoryview
  Coverage: `tests/fixtures/core/binary_buffers.py`,
  `tests/fixtures/core/builtin_types_edges.py`,
  `tests/fixtures/compat_sections/builtins.py`
  Completed: bytes and bytearray operations plus memoryview native scalar
  formats, signed/unsigned/float/character decoding and writes, shaped casts,
  multidimensional metadata/indexing/lists, exporter resize locking, readonly
  derivation, release idempotence, and released-view exception typing. Exotic
  external PEP 3118 exporters depend on the separately deferred native CPython
  extension ABI and are outside this builtin-runtime row.

