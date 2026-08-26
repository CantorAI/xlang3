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
