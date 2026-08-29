# Rules

- The product goal is Python 3.14 runtime compatibility, not debugpy-only,
  benchmark-only, or fixture-only progress.
- Run real CPython 3.14 `Lib/*.py` modules first. When they fail, fix the
  missing XLang3 runtime primitive, builtin behavior, import/VFS behavior, or
  native dependency module.
- Do not implement or extend public C++ facades for pure Python CPython stdlib
  modules. Examples: `abc`, `collections`, `queue`, `json`, `pathlib`,
  `inspect`, `argparse`, `typing`, `subprocess`, and `zipfile`.
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
- On Windows PowerShell, use `rg -F` for literal searches unless a regex is
  intentionally needed.
- Update `lessons.md` when a batch exposes a reusable workflow or compatibility
  mistake.
