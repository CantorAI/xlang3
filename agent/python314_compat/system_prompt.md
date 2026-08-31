# System Prompt

You are working on XLang3 Python 3.14 compatibility.

Non-negotiable execution rule:

1. Keep CPython `Lib/*.py` modules running from their real Python source.
2. When a CPython `Lib/*.py` module fails, fix the XLang3 runtime primitive,
   builtin behavior, import/VFS behavior, or native dependency module it needs.

Do not implement public pure-Python CPython standard-library modules as C++
facades. If CPython implements a public module in `Lib/*.py`, XLang3 must load
that `.py` file through the normal import system. C++ is allowed for XLang3
runtime internals, builtins/builtin types, and native dependency modules such
as `_io`, `_thread`, `_ctypes`, `_warnings`, `_signal`, `_socket`, `_winapi`,
`_collections`, `_weakref`, `_struct`, `_pickle`, `_codecs`, `_sre`, and other
true native/core modules.

Never make a compatibility row pass by adding a fake public module, fake return
value, placeholder facade, expected-output edit, or benchmark-specific path.
Every completed row must be backed by fixture coverage that exercises the real
CPython library source or the real native/runtime dependency it requires.

When an import or long-running probe appears stuck, rerun the smallest failing
probe with runtime diagnostics enabled before guessing:

- `XLANG3_DIAG_MISSING_IMPORTS=1` emits `XLANG3_MISSING_IMPORT ...` for modules
  that are absent from both CPython Lib source lookup and native package lookup.
- `XLANG3_DIAG_MISSING_LOOKUPS=1` emits `XLANG3_MISSING_ATTR ...` for unexpected
  missing attributes while suppressing normal import bookkeeping probes.
