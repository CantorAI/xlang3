<!--
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->
# Visual Studio Debugpy Compatibility Audit

Status: paused implementation audit

## Goal

Make the XLang3 Windows `python.exe` alias natural enough that Visual Studio can
launch its bundled Python debugger path with XLang3 instead of CPython.

The target is not a debugpy-specific shortcut. The target is ordinary Python
3.14 compatibility for the syntax, import system, modules, frame objects, trace
hooks, and threading behavior that debugpy and pydevd use.

## Current Evidence

Visual Studio starts its bundled adapter by running an interpreter with a script
argument similar to:

```text
D:\CantorAI\xlang3\build\Release\python.exe
  "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\Extensions\Microsoft\Python\Core\debugpy\adapter"
```

XLang3 now gets into the debugpy/pydevd import chain, but it is not complete.

Latest focused probe:

```text
XLANG3_PYTHON_LIB=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\Extensions\Microsoft\Python\Core
build\Release\python.exe scratch\pydevd_import_probe.py
```

Current blocker:

```text
parse error importing module 'tokenize':
line 60, column 22: expected newline after function header
```

The failing CPython 3.14 code is ordinary syntax:

```python
def group(*choices): return '(' + '|'.join(choices) + ')'
```

This proves the next step is Python syntax compatibility, not a debugpy
special-case.

## Recent Compatibility Work

- `enum` module foundation was added so pydevd can import enum-backed flags.
- Subclasses of builtin scalar classes can use inherited builtin constructors,
  which is needed by `IntEnum` / `IntFlag`-style classes.
- `os.scandir()` and `DirEntry` foundation were added for file-watching code.
- Function and native-function objects expose `__doc__` as `None` when no doc
  value is attached.
- The lexer now avoids treating `'''` or `"""` inside a normal string literal as
  a triple-quoted string opener.

These are generic Python compatibility improvements, but they are incomplete
and must be audited with product tests before being treated as done.
The canonical debt list for these recent changes lives in
`python314-compat-audit.md` under "Recent Compatibility Debt".

## Compatibility Gaps Exposed By Debugpy

### Syntax

- [ ] One-line compound suites:
  `def f(): return x`, `class C: pass`, `if x: y`, `while x: y`,
  `for x in y: z`, `try: x`, `except E: y`.
- [ ] Full function annotations used by debugpy public APIs:
  PEP 604 unions, generic aliases, and complex annotations must parse and lower
  without changing runtime behavior.
- [ ] Complete f-string behavior, including CPython 3.14 edge cases.
- [ ] Full exception-handler syntax and behavior audit for pydevd paths.

### Import And Module Metadata

- [ ] Native and Python modules must expose compatible `__file__`,
  `__package__`, `__spec__`, `__loader__`, and package path metadata.
- [ ] Package imports must behave correctly for directory packages, vendored
  packages, and script-directory execution.
- [ ] `runpy`, `importlib`, and package execution must be tested against
  debugpy adapter/launcher entry points.

### Python Object Model

- [ ] Function objects need a full metadata audit:
  `__name__`, `__qualname__`, `__module__`, `__doc__`, `__defaults__`,
  `__kwdefaults__`, `__annotations__`, `__dict__`, `__code__`.
- [ ] Code objects need enough CPython shape for debugpy:
  `co_name`, `co_qualname`, `co_filename`, `co_firstlineno`, `co_flags`,
  `co_argcount`, `co_posonlyargcount`, `co_kwonlyargcount`, `co_varnames`,
  `co_names`, `co_consts`, line-table behavior.
- [ ] Frame objects need enough CPython shape:
  `f_back`, `f_code`, `f_globals`, `f_locals`, `f_lineno`, `f_trace`,
  thread association, and stable locals snapshots.
- [ ] Class/type behavior needs an audit for metaclasses, descriptors,
  classmethod/staticmethod/property, MRO, `super()`, and class attributes.

### Standard Library Surface

- [ ] `tokenize` must import and run from CPython 3.14 `Lib`.
- [ ] `linecache`, `inspect`, `traceback`, `weakref`, `threading`, `queue`,
  `socket`, `select`, `subprocess`, `ctypes`, `logging`, `pathlib`, `os`,
  `sysconfig`, and `site` need compatibility tests driven by debugpy imports.
- [ ] Native modules added for compatibility must be reviewed to avoid
  debugpy-only semantics.

### Debug Runtime Surface

- [ ] `sys.settrace`, `threading.settrace`, and per-frame `f_trace` must behave
  like Python APIs over XlangVM state.
- [ ] Trace events must be correct for call, line, return, exception, generator,
  coroutine, and native-call boundaries.
- [ ] Multiple native threads must receive compatible trace setup.
- [ ] Breakpoint, step-in, step-over, step-out, pause, and evaluate must be
  verified through a real IDE flow, not only a CLI probe.

## Product-Ready Implementation Order

1. Parser compatibility batch:
   one-line suites, annotations used by debugpy, full stdlib string literal
   edge cases, and parser tests against `tokenize.py`.

2. Import/module metadata batch:
   `__file__`, `__spec__`, `__loader__`, package path behavior, `runpy`, and
   debugpy adapter/launcher import entry points.

3. Function/code/frame object batch:
   complete Python-visible metadata and tests comparing CPython 3.14 output to
   XLang3 output.

4. Stdlib dependency batch:
   run a deterministic import ladder for Visual Studio's bundled debugpy and
   promote each missing generic module behavior into the Python 3.14 audit.

5. Trace/debug runtime batch:
   implement trace semantics over XlangVM and verify with small Python tests
   before using Visual Studio as the final integration test.

6. Visual Studio integration batch:
   register/use `D:\CantorAI\xlang3\build\Release\python.exe` as an interpreter,
   launch a normal `.py` file through VS Python Debug, hit breakpoints, inspect
   locals/class attributes, step into methods, evaluate expressions, and exit
   cleanly.

## Rules For Continuing

- Do not add code that detects `debugpy`, `pydevd`, or a specific benchmark and
  changes behavior only for that case.
- Existing pydevd/debugpy shim source, if kept, is only diagnostic debt unless
  explicitly wired as part of an XLang3-owned debug adapter. It must not be used
  to claim CPython/debugpy compatibility.
- Every fix must be expressible as Python 3.14 compatibility, XLang3 runtime
  architecture, or debugger API compatibility.
- A checklist item becomes `[x]` only after a CPython-vs-XLang3 test exists and
  passes for the declared scope.
- Keep native implementations modular, but do not use native modules to hide
  missing core semantics.
