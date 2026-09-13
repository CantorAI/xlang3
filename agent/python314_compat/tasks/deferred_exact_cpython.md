# Deferred Exact CPython Tasks

- [x] deep Unicode database
  Coverage: `tests/fixtures/compat_sections/strings_and_unicode.py`,
  `tests/fixtures/core/unicode_database_edges.py`.
  Completed: generated Unicode 16.0 and frozen Unicode 3.2.0 tables provide
  names and canonical lookup, aliases and named sequences for the current
  database, every exposed property, numeric/decomposition data, full casing,
  identifier/classification flags, and NFC/NFD/NFKC/NFKD normalization. The
  legacy normalizer includes CPython's five retroactive correction mappings.
  Unicode-aware strip/split/rsplit/splitlines and padding boundaries are
  differential-covered. Every CPython 3.14 encoding module name loads with the
  same sole non-codec exception (`_win_cp_codecs`); `_bz2`, Windows ANSI/MBCS/
  DBCS, and OEM support are native dependencies beneath source-backed CPython
  `bz2.py` and `encodings` modules. Grapheme segmentation is not a CPython
  builtin `str` or `unicodedata` API.

- [x] pyc cache policy
  Coverage: import fixtures.
  Completed: XLang3 may emit `.pyc` compatibility artifacts but intentionally keeps
  Python source authoritative and does not reuse CPython cache bytecode.

- [x] frozen bytecode policy
  Coverage: import fixtures.
  Completed: XLang3 uses native bootstrap protocol modules and intentionally does not
  embed CPython's frozen pure-library bytecode table.

- [x] native CPython extension compatibility boundary
  Coverage: `tests/cpython_bridge/test_import.py`, `test_python_objects.py`,
  `test_lifetime.py`, `test_shutdown.py`, `test_hosted.py`,
  `test_hosted_lifetime.cpp`, `test_buffers.py`, `test_protocols.py`,
  `test_snapshots.py`, `test_reducers.py`, and `test_classes.py`.
  Completed: ABI-heavy `PyInit_*` extensions run in the matching embedded
  CPython 3.14 interpreter through the optional `cpython` native package and
  cross the runtime boundary as retained live proxies, buffers, or explicit
  trusted snapshots. Representative compiled `_sha2` import, callbacks,
  identity, refcount/lifetime, threading, exceptions, buffer leases, repeated
  runtime teardown, and C++ SDK calls are covered. NumPy-style packages use
  `cpython.importModule()` and remain CPython-owned. Direct `PyObject*` layout/
  refcount simulation is intentionally excluded by the XLang3 implementation
  spec because XLang3 retains its own `X3Value` object and refcount model. The
  bridge README records the loader strategy and memory-corruption risks of a
  partial CPython ABI clone. The optional build now discovers the selected host
  Python root and stages `python314.dll` beside the hosted native package.

