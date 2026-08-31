# Rules

- The product goal is Python 3.14 runtime compatibility, not debugpy-only,
  benchmark-only, or fixture-only progress.
- Run real CPython 3.14 `Lib/*.py` modules first. When they fail, fix the
  missing XLang3 runtime primitive, builtin behavior, import/VFS behavior, or
  native dependency module.
- Enforced boundary: do not implement or extend public C++ facades for pure
  Python CPython stdlib modules. Examples include `asyncio`, `ctypes`,
  `threading`, `warnings`, `signal`, `abc`, `collections`, `queue`, `json`,
  `pathlib`, `inspect`, `argparse`, `typing`, `subprocess`, and `zipfile`.
- Before adding a public module name in C++, check CPython 3.14
  `importlib.util.find_spec(name)`. If it resolves to `Lib/*.py`, implement the
  needed runtime behavior or native dependency such as `_ctypes`, `_thread`,
  `_warnings`, or `_signal` instead.
- Native C++ is correct for XLang3 runtime internals, builtins/builtin types,
  native dependency modules such as `_io`, `_thread`, `_weakref`, `_abc`,
  `_collections`, `_struct`, `_pickle`, `_socket`, `_winapi`, `time`, `zlib`,
  and product-specific accelerated modules.
- Do not hide gaps with stubs, placeholder imports, fake return values, broad
  locks, sleeps, retries, or expected-output edits.
- Every compatibility change needs fixture coverage under `tests/fixtures`.
  Mark a task `[x]` only when the scoped behavior is implemented, tested, and
  has no known remaining gap.
- If validation fails, repair the current failed batch before advancing. Treat
  crashes, hangs, Windows popups, and negative exits as runtime regressions.
- Use deterministic scripts: `agent/scripts/build_release.py`,
  `agent/scripts/run_fixtures.py`, and `agent/scripts/run_section_fixture.py`.
- `agent/scripts/build_release.py` runs `check_module_boundaries.py`; never
  bypass this check when adding or removing stdlib/native modules.
- On Windows PowerShell, use `rg -F` for literal searches unless a regex is
  intentionally needed.
- Update `lessons.md` when a batch exposes a reusable workflow or compatibility
  mistake.
