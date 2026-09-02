# Goal

Make XLang3 runtime compatible with Python 3.14 so CPython standard-library
`.py` files can run naturally on XLang3.

This does not mean replacing XLang3 with CPython:

- Keep XLang3 value/object/runtime architecture.
- Keep the XLang3 ref-count/object model.
- Keep ProgramIR and XlangVM execution.
- Do not redesign around `PyObject*`.

Compatibility means the runtime APIs, object behavior, import system, builtins,
exceptions, frames, code objects, file APIs, and required native dependency
modules are compatible enough for Python 3.14 library code.
