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

Status: product direction

XLang3's debugging goal is Python-source debugging over XlangVM, not CPython
debugpy emulation as the core architecture.

## Goal

Debuggers should be able to inspect and control Python code running on XLang3:

```text
IDE debugger UI
  -> DAP transport
    -> XLang3 native debug adapter
      -> DebugSession
        -> Runtime debug state
          -> XlangVM
```

The debugger talks DAP. XLang3 owns the server side in C++.

## Non-Goal

XLang3 does not depend on `debugpy/adapter` for normal debugging.

CPython needs `debugpy/adapter` because `python.exe` does not speak DAP by
itself. XLang3 already has an internal DAP server, so the product path is:

```text
xlang3 --dap-stdio
```

not:

```text
python.exe path/to/debugpy/adapter
```

If XLang3 later becomes able to run debugpy as ordinary Python code, that is an
extra compatibility path, not the primary debugger architecture.

## Executable Model

`xlang3.exe` is the CLI host. The runtime implementation is in
`xlang3_runtime`.

Windows release layout:

```text
xlang3.exe
python.exe
xlang3_runtime.dll
```

`python.exe` is a Python-compatible alias for running `.py` programs and Python
CLI forms. It should not secretly reinterpret `debugpy/adapter` as an internal
debugger request. Native debugging uses the explicit adapter entry:

```text
xlang3.exe --dap-stdio
python.exe --dap-stdio
```

## Native DAP Adapter Contract

`--dap-stdio` reserves stdio for DAP framing:

```text
Content-Length: N\r\n\r\n{json}
```

Program output is captured and emitted as DAP `output` events so user `print()`
does not corrupt the protocol stream.

Initial DAP command surface:

```text
initialize
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
evaluate
disconnect
terminate
```

The DAP layer must stay transport-only. It must not duplicate VM stepping,
breakpoint, or frame-stack logic.

## Runtime Ownership

Debugger control is layered like this:

```text
DapSession
  -> DebugSession
    -> Runtime debug state
      -> XlangVM frame stack / instruction pointer / source map
```

`DebugSession` owns:

- loaded source/module
- current `Runtime`
- current `Interpreter`
- breakpoints
- pause/resume state
- current paused frame stack

`Runtime` owns:

- debug enabled flag
- poll-needed flag
- step policy
- breakpoint table
- trace hooks
- paused frame materialization

`XlangVM` owns:

- actual frame stack
- instruction pointer
- source-line transitions
- call/return/exception events
- pause/resume continuation

## IDE Integration

### Visual Studio

Visual Studio can debug XLang3 through its Debug Adapter Host when launched
with a DAP launch JSON that points to:

```json
{
  "$adapter": "D:\\CantorAI\\xlang3\\build\\Release\\xlang3.exe",
  "$adapterArgs": "--dap-stdio",
  "type": "xlang3",
  "request": "launch",
  "program": "D:\\path\\to\\app.py"
}
```

Product packaging may later add a VSIX or environment registration so users do
not hand-write this file, but the adapter protocol stays the same.

### VS Code

VS Code needs a small extension to register `type: "xlang3"`. The extension
only starts:

```text
xlang3 --dap-stdio
```

It must not contain debugger semantics.

## Python Debug API Compatibility

Even with native DAP, XLang3 still needs Python-visible debug APIs because user
code and Python libraries expect them:

```python
sys.settrace(fn)
sys.gettrace()
sys._getframe(depth=0)
threading.settrace(fn)
threading.gettrace()
```

Frame/code objects must be XLang3 objects with Python-compatible shape:

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

These APIs are not CPython internals. They are Python-compatible views over
XlangVM state.

## Evaluate

DAP `evaluate` should execute a real Python expression in the selected paused
frame:

```text
expression + selected frame id
  -> parse expression
  -> bind against frame locals/globals
  -> execute in XlangVM eval mode
  -> return X3Value result
```

Temporary identifier heuristics are not product behavior. Hover, watch, and
debug console expressions should all use the same expression execution path.

## Performance Rules

When debugging is disabled:

- no DAP objects are created
- no frame snapshots are materialized
- no breakpoint map lookup occurs in the normal path
- VM debug polling is controlled by a cached runtime flag
- the branch should be cold/unlikely

When debugging is enabled but no breakpoints/steps/hooks are active, the VM
should keep the poll flag off.

When step or breakpoint logic is active, XLang3 may temporarily disable inline
call shortcuts in affected user frames so source stepping remains correct.

## Device/Pico Direction

Pico/device builds should not run a desktop DAP server or debugpy.

Device side should expose a compact debug/RPC command channel backed by the
same VM debug primitives:

```text
host DAP adapter
  -> device debug RPC
    -> XlangVM debug state
```

This keeps the device runtime small while preserving one debugger model.

## Phases

### D0: Native DAP Core

- [x] `xlang3 --dap-stdio`
- [x] DAP framing
- [x] initialize/launch/continue/step/stack/scopes/variables basics
- [x] program output as DAP output events

### D1: Source Debug Semantics

- [x] line breakpoints
- [x] step into
- [x] step over
- [x] step out
- [x] pause/resume VM frame stack
- [~] exception breakpoints
- [~] conditional breakpoints
- [~] hit counts

### D2: Variable Inspection

- [x] locals/globals scopes
- [x] object/list/tuple/dict/set/module expansion
- [x] class and instance attributes
- [~] variable mutation
- [~] rich type display

### D3: Real Evaluate

- [~] evaluate selected-frame expression using the Python expression parser and runtime value/object helpers
- [~] watches use the same DAP evaluate path
- [~] hovers use the same DAP evaluate path
- [~] debug console uses the same DAP evaluate path
- [ ] migrate evaluator backend to VM eval mode for full expression coverage

### D4: Thread And Async Debug

- [~] runtime thread foundations
- [ ] DAP threads view for native Python threads
- [ ] per-thread frame stacks
- [ ] async task/coroutine presentation

### D5: IDE Packaging

- [~] Visual Studio manual Debug Adapter Host launch JSON
- [~] VS Code extension skeleton
- [ ] Visual Studio product registration/package
- [ ] VS Code extension validation

### D6: Optional Debugpy Compatibility

- [ ] run `debugpy` as normal Python code
- [ ] no CLI magic for `debugpy/adapter`
- [ ] debugpy uses XLang3 `sys.settrace`/frame APIs if users choose that path
