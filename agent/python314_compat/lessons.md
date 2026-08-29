# Compatibility Loop Lessons

Keep this file short. Add only reusable lessons that should shape future
batches.

## Product Direction

- The goal is Python 3.14 runtime compatibility. Do not optimize for debugpy,
  benchmarks, or isolated fixture tricks.
- Pure Python CPython stdlib modules should run from the real `Lib/*.py` source.
  Do not add public C++ facades for modules such as `abc`, `collections`,
  `queue`, `json`, `pathlib`, `inspect`, `argparse`, `typing`, `subprocess`, or
  `zipfile`.
- Native C++ is for XLang3 runtime primitives, builtins/builtin types, CPython
  native dependency modules, and product-specific accelerated modules.
- When a pure stdlib import fails, fix the runtime primitive or native
  dependency it exposes. Do not make the import pass with a stub.

## Validation

- Every compatibility claim needs fixture coverage under `tests/fixtures`.
- Do not update expected output just to pass. Compare with Python 3.14 behavior
  or the intended XLang3 runtime contract first.
- Treat crashes, hangs, Windows popups, negative exits, and timeout regressions
  as runtime bugs. Add the smallest fixture repro before fixing.
- If validation fails, repair the current batch before advancing to a new task.

## Workflow

- Use deterministic scripts: `agent/scripts/build_release.py`,
  `agent/scripts/run_fixtures.py`, and `agent/scripts/run_section_fixture.py`.
- Use `rg -F` for literal PowerShell searches involving checkboxes, brackets,
  backticks, quotes, C++ punctuation, or Python syntax.
- Do not start a second loop while one is running. Use the loop lock and
  stop-request file.
- Decode captured child process output as UTF-8 with replacement on Windows.

## Runtime Compatibility

- Descriptor changes usually need both generic attribute lookup and VM fast-path
  attribute lookup updated together.
- Native functions that should raise catchable Python exceptions must use the
  runtime exception path, not raw error strings.
- Keyword handling and arity diagnostics are CPython-visible behavior; fixture
  both accepted calls and rejection text when changing call binding.
- Builtin constructor fast paths must preserve `__new__`, `__module__`,
  `__qualname__`, and class statement behavior.
- Exception behavior must follow inheritance, not name suffixes like `Error` or
  `Exception`.
- Structseq behavior belongs in shared helpers. Cover construction policy,
  named fields, repr/str, reduce/pickle payloads, and descriptor behavior.
- Import compatibility includes visible metadata and cache side effects:
  `sys.modules`, `__loader__`, `__spec__`, `__file__`, `sys.path[0]`, and
  `sys.path_importer_cache`.
- `sys.exc_info()` and `sys.exception()` are scoped to active exception
  handlers; nested handlers must restore previous state correctly.
- File/path APIs must preserve CPython's distinct `str`/`bytes`/`os.PathLike`
  conversion errors.
- Text streams return `str`; binary/buffer layers return `bytes`.
- Threading changes must define ownership and locking invariants. Lock the
  minimum region and do not serialize the whole VM as a shortcut.
