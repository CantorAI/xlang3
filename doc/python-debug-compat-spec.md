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
# Python Debug Compatibility Spec

Status: implemented for the Visual Studio 2026 debugpy launch path

## Visual Studio architecture

Visual Studio uses its normal Python Debug Adapter Host and bundled debugpy
components. CPython 3.14 hosts the adapter and launcher. XLang3 is selected as
the Python interpreter for the debuggee:

```text
Visual Studio Debug Adapter Host
  -> Visual Studio debugpy adapter and launcher (CPython 3.14 host)
    -> xlang3.exe
      -> debugpy/pydevd server running as Python on XlangVM
        -> user Python program running on XlangVM
```

The Visual Studio path does not use XLang3's native `--dap-stdio` adapter.
That adapter remains an independent XLang3 capability.

## Runtime contract

XLang3 supplies the Python 3.14 runtime behavior consumed by unmodified
debugpy and pydevd:

- normal Python CLI script and module launch forms;
- source-backed CPython `Lib` modules and Visual Studio's source-backed debugpy;
- `sys.monitoring`, tracing, profiling, frames, code objects, and source metadata;
- stable per-thread frame identity, locals, globals, and back links;
- `_thread`, `threading`, queues, events, sockets, and blocking operations;
- Python-compatible process, path, and module metadata.

Pure CPython library modules remain Python source. C++ implements VM semantics
and native dependencies only; no public C++ facade replaces a pure `Lib/*.py`
module.

## Required behavior

The integration test must use the installed Visual Studio adapter without
patching it. It must:

1. initialize a DAP session;
2. launch `xlang3.exe` as the selected Python interpreter;
3. set and verify a source breakpoint;
4. stop in the XLang3 user frame;
5. inspect the local `value` as `41`;
6. continue and observe output `42`;
7. receive termination and disconnect cleanly.

Repository configuration and the deterministic protocol test are in
`tests/ide/vs2026_launch.json` and `tests/cli/run_debugpy_launch_smoke.py`.

## Native DAP

`xlang3 --dap-stdio` remains available for XLang3-owned clients and tests. It
uses the VM's native debug state, breakpoint, stepping, frame, and evaluation
support. It is outside the Visual Studio debugpy compatibility route described
above.

## Device direction

Embedded targets should expose compact debug/RPC operations backed by the same
XlangVM primitives. Desktop debugpy and Visual Studio components are not part
of device builds.
