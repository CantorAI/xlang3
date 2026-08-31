# Module Policy

XLang3 compatibility is runtime-first.

The required order is:

1. Keep CPython `Lib/*.py` modules running from their real Python source.
2. Fix the XLang3 runtime/native dependency exposed by the failure.

Pure Python CPython standard-library modules must run from Python source. Do
not satisfy them by adding public C++ facades. This is an enforced product
boundary, not a preference.

Native C++ belongs in:

- XLang3 runtime core: values, objects, frames, calls, exceptions, import, VFS,
  IR, and XlangVM.
- Builtins and builtin types.
- CPython native dependency modules, usually named with a leading underscore:
  `_io`, `_thread`, `_abc`, `_weakref`, `_collections`, `_queue`, `_socket`,
  `_signal`, `_stat`, `_struct`, `_pickle`, `_winapi`, plus native modules such
  as `errno`, `time`, `marshal`, `zlib`, `winreg`, and `unicodedata`.
- XLang3 product modules and package ABI support.

When `import typing`, `import inspect`, `import argparse`, or another pure
stdlib module fails, fix the runtime/native dependency it exposes. Do not add a
native public module just to make the import pass.

If a CPython module is implemented in `Lib/*.py`, XLang3 must not register that
same public name from C++. Implement the native dependency layer instead:
`ctypes` needs `_ctypes`, `threading` needs `_thread`, `warnings` needs
`_warnings`, and `signal` needs `_signal`.

Task rows are done only when the real CPython-compatible behavior is
fixture-covered.
