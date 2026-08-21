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
# Python Debugpy Compatibility Spec

Status: design target

XLang3's product goal is full Python 3.14 syntax and runtime compatibility while keeping the XLang runtime model:

- `X::Value` / `X3Value` remains the universal value representation.
- XLang3 does not become CPython internally.
- XLang3 executes `.py` through XLang3 parser, AST, IR, and XlangVM.
- Python-compatible runtime APIs are implemented on top of XLang3 runtime objects.

This document focuses only on debugging compatibility.

## Goal

Existing Python debugger integrations should be able to debug Python code running on XLang3 with minimal IDE-side changes.

Primary target:

```text
Visual Studio / VSCode / other Python debugger UI
  -> existing debugpy adapter / launcher / server Python code
    -> XLang3 python-compatible CLI
      -> XLang3 runtime trace/frame APIs
        -> XlangVM
```

The important compatibility point is not the file extension alone. The important point is that XLang3's `python.exe`-compatible CLI can run the same debugpy command shapes that existing tools launch for CPython.

## Executable Model

`xlang3.exe` is the CLI host. The real implementation is in `xlang3_runtime`.

For Python tooling compatibility, release packages may include:

```text
xlang3.exe
python.exe
pythonw.exe
xlang3_runtime.dll
```

`python.exe` may be a copy, hardlink, or launcher for `xlang3.exe`. Both `xlang3.exe` and `python.exe` should support Python-compatible CLI semantics.

There should not be a separate XLang-only CLI language mode. XLang-specific options are additive extensions.

Required CLI forms:

```text
python.exe script.py arg1 arg2
python.exe -c "print(1)"
python.exe -m module arg1
python.exe -V
python.exe --version
python.exe -X frozen_modules=off script.py
python.exe path/to/package_or_module_dir --connect ... script.py
python.exe path/to/debugpy/launcher <port> -- script.py
python.exe path/to/debugpy/adapter
```

The same forms should work when invoked as `xlang3.exe`.

## Observed Visual Studio Debugpy Launch Shape

Visual Studio may not launch `debugpy-adapter.exe` directly. It can launch debugpy adapter and launcher as Python files/modules through `python.exe`.

Observed command shapes:

```text
python.exe "...Visual Studio...\debugpy\adapter"

python.exe "...Visual Studio...\debugpy\launcher" <port> -- script.py

python.exe -X frozen_modules=off "...Visual Studio...\debugpy"
  --connect 127.0.0.1:<port>
  --configure-subProcess False
  --configure-qt none
  --adapter-access-token <token>
  script.py
```

Therefore, XLang3 compatibility must focus on running these Python entry files/packages correctly, not on providing a native `debugpy-adapter.exe`.

## Debugpy Reuse Strategy

`debugpy` and its vendored `pydevd` logic are mostly Python code. XLang3 should try to run the existing debugpy code by implementing the Python runtime APIs it depends on.

This is preferred over writing a custom debugger first because:

- Visual Studio already knows how to launch debugpy.
- VSCode Python debugger already knows debugpy.
- Other tools may also understand debugpy-style Python debugging.
- XLang3 can reuse existing adapter, launcher, breakpoint, stepping, and variable protocol code.

This does not mean XLang3 becomes CPython. It means XLang3 exposes Python-compatible debugger APIs backed by XlangVM state.

## Required Runtime API Surface

The debugger-facing Python runtime APIs must become first-class XLang3 runtime objects and modules.

Core modules and functions:

```python
sys.settrace(fn)
sys.gettrace()
sys._getframe(depth=0)
threading.settrace(fn)
threading.gettrace()
```

Frame/code objects:

```python
frame.f_back
frame.f_code
frame.f_globals
frame.f_locals
frame.f_lineno
frame.f_trace

code.co_filename
code.co_name
code.co_firstlineno
```

Trace events:

```text
call
line
return
exception
```

The objects are real XLang3 Python-compatible objects, not CPython objects. Internally:

```text
frame object -> XlangVMFrame
code object  -> IR function metadata and source map
line number  -> function_id + ir_ip -> source line
locals       -> local slots exposed through dict/proxy
globals      -> module slots exposed through module/dict/proxy
```

## XlangVM Debug Hook

The VM must provide a low-overhead trace hook equivalent to CPython's tracing callbacks.

Conceptual shape:

```cpp
if (trace_enabled && source_line_changed) {
  call_trace_function(frame_object, "line", None);
}
```

The hot path must remain fast when tracing is disabled:

- compile-time option may remove debug hooks for minimal builds
- runtime hook pointer is null when tracing is disabled
- branch should be marked unlikely
- no breakpoint map lookup in the normal no-debug path

The trace hook is not a Pico communication mechanism and should not own transport pumping. It is the VM execution event source for debugging.

## Debug Session Controller

The desktop debugger path should use a reusable runtime controller before any
socket or DAP transport code:

```text
DAP/debugpy transport
  -> XLang3 DebugSession
    -> Runtime debug state
    -> Interpreter pause/resume
    -> XlangVM
```

`DebugSession` owns the loaded module, `Runtime`, `Interpreter`, current paused
state, breakpoint state, and source location status. It exposes debugger verbs:

```text
launch
continue
step into
step over
step out
request pause
add/clear breakpoints
```

The DAP server must call this controller instead of duplicating VM control
logic. This keeps the transport layer small and preserves one source of truth
for frame-stack pause/resume behavior.

## Native DAP Adapter Track

XLang3 also has a native C++ DAP adapter track for desktop debugging. This is
not a debugpy rewrite and it is not the Pico/device protocol. It is a direct
DAP transport binding over `DebugSession`:

```text
IDE DAP client
  -> XLang3 native DAP adapter
    -> DebugSession
      -> Runtime debug state
      -> XlangVM
```

The adapter owns only protocol work:

- read and write DAP framing: `Content-Length: N\r\n\r\n{json}`
- dispatch debugger commands
- emit DAP responses and events as separate protocol messages
- translate paused XLang3 frames/scopes/variables into DAP shape

Initial command surface:

```text
initialize
initialized event
launch
setBreakpoints
setExceptionBreakpoints
configurationDone
continue
next
stepIn
stepOut
threads
stackTrace
scopes
variables
output event
terminated event
disconnect
terminate
```

The adapter must not duplicate VM stepping logic. Breakpoint ownership,
pause/resume state, and stack-frame state stay inside `DebugSession` and the
runtime debug hooks.

The first adapter process mode is:

```text
xlang3 --dap-stdio
```

In stdio DAP mode, stdout is reserved for framed DAP protocol messages. Program
stdout is captured by the session and emitted as DAP `output` events, so user
`print()` calls do not corrupt the protocol stream. A socket mode can be layered
over the same `DapSession` protocol core later.

### VS Code Registration

VS Code does not launch arbitrary DAP executables from `launch.json` alone. It
needs a debug type contributed by an extension. XLang3 keeps this layer small:

```text
tools/vscode/xlang3-debug
  -> registers debug type: xlang3
  -> starts: xlang3 --dap-stdio
```

The extension contributes an `XLang3: Current Python File` launch shape:

```json
{
  "name": "XLang3: Current Python File",
  "type": "xlang3",
  "request": "launch",
  "program": "${file}",
  "adapterPath": "D:/CantorAI/xlang3/build/Release/xlang3.exe",
  "args": []
}
```

The adapter path can also come from:

```text
xlang3.debugAdapterPath
XLANG3_EXE
xlang3 on PATH
```

This registration layer must remain transport-only. It should not contain VM,
breakpoint, or stepping logic.

## Source Mapping

The IR must carry enough source mapping for Python-level debugging:

```text
module file
function id
IR instruction index
source line
source column when available
statement boundary marker
```

Breakpoints are line-based at first:

```text
file.py:line -> one or more IR instruction indices
```

Stepping uses source-line transitions, not every IR instruction.

## Debugpy Import Compatibility

To run debugpy, XLang3 must support a broader Python runtime subset. Initial likely requirements:

```text
sys
os
time
threading
_thread
socket
json
queue
traceback
inspect
runpy
importlib
types
collections
weakref
atexit
logging
pathlib
```

Not all modules need to be Python-source implementations. XLang3 may provide native modules where performance or implementation practicality requires it, while keeping Python-compatible import behavior.

## Phased Plan

### Phase D0: Python CLI Compatibility

- `xlang3.exe` and `python.exe` accept Python CLI forms.
- Implement `-c`, `-m`, script path, `--`, `-V`, `--version`.
- Accept/ignore safe CPython options such as `-X frozen_modules=off`.
- Populate `sys.argv`, `sys.executable`, `sys.path`, `sys.prefix`.

### Phase D1: Run Debugpy Entry Points

- Run `debugpy` package from Python 3.14 site-packages.
- Run Visual Studio-style direct entry paths:

```text
python.exe path\to\debugpy\adapter
python.exe path\to\debugpy\launcher ...
python.exe path\to\debugpy --connect ...
```

- Fill missing module/runtime APIs until debugpy imports and starts.

### Phase D2: Trace And Frame API

- Implement `sys.settrace`, `sys.gettrace`, `threading.settrace`.
- Add XLang3 frame/code objects.
- Add VM trace events for call, line, return, exception.
- Add source maps in IR.

### Phase D3: Breakpoints And Stepping Through Debugpy

- Let debugpy/pydevd set line breakpoints.
- Stop on line events.
- Support continue, step over, step in, step out.
- Expose stack frames and local/global variables.

### Phase D4: IDE Validation

- Validate Visual Studio launch/debug.
- Validate VSCode Python debugger launch/debug.
- Validate attach mode where the debug server is started from user code.

### Phase D5: Device/Pico Debug Bridge

For Pico, do not run debugpy on device.

Host side:

```text
IDE/debugpy or XLang3 debug adapter
  -> host bridge
  -> compact XLang3 debug RPC
```

Device side:

```text
XlangVM debug agent
  -> source map / frames / variables
  -> compact RPC only
```

The device runtime must remain no-OS friendly and must not depend on Python debugpy.

## Non-Goals For This Spec

- CPython C extension ABI compatibility.
- Replacing XlangVM with CPython.
- Making Pico run full debugpy.

These may be separate tracks later.

## Success Criteria

Minimum desktop success:

```text
python.exe -m debugpy --listen 5678 --wait-for-client app.py
```

runs on XLang3 and allows an IDE to:

- set a breakpoint in `app.py`
- stop at that source line
- show call stack
- show locals/globals
- continue
- step over a line

Visual Studio success:

```text
Visual Studio launches XLang3's python.exe as the active Python environment
Visual Studio's existing Python debugger can debug a .py file running on XLang3
```

VSCode success:

```text
VSCode Python debugger can use XLang3's python.exe interpreter path
and debug a .py file running on XLang3
```
