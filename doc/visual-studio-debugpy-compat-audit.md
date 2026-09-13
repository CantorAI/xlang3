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

Status: direct `xlang3.exe` launch validated; performance work in progress

## Validated topology

Visual Studio's Python Debug Adapter Host launches the installed, unmodified
`debugpy.adapter` with `build/Release/xlang3.exe`. The adapter launches
`debugpy.launcher` with `xlang3.exe`, and the launcher starts the debug server
and target program with `xlang3.exe` on XlangVM.

The validated flow is:

```text
Visual Studio -> xlang3.exe/debugpy adapter -> xlang3.exe/debugpy launcher -> xlang3.exe/user code
```

No `python.exe` alias, installed CPython runtime, or custom DAP bridge is part
of this flow.

## Evidence

`tests/cli/run_debugpy_launch_smoke.py` loads the repository's Visual Studio
launch JSON and drives the complete DAP exchange against Visual Studio 2026's
installed Python Core directory. The test verifies a
breakpoint at line 17 of `tests/ide/vs2026_debug_smoke.py`, reads local variable
`value` as `41`, continues, issues an asynchronous Pause, validates the paused
stack, resumes, observes `42`, and requires debuggee termination and a
successful DAP disconnect. Visual Studio owns the adapter process lifetime;
the regression applies the same process-tree cleanup if the adapter remains in
debugpy's session wait loop after disconnect.

The repository-root `launch.vs.json` provides a normal selectable Visual Studio
Startup Item profile for F5 debugging. `tests/ide/vs2026_launch.json` provides
the equivalent low-level Visual Studio Debug Adapter Host launch.
`tests/ide/README.md` documents both paths.
The CMake test is named `xlang3_cli_visual_studio_debugpy_launch`. The adapter
smoke also runs `debugpy_bootstrap_compat.py`, whose stable output is checked
against both CPython 3.14 and XLang3 before the DAP lifecycle test.

PTVS sends `stopOnEntry: true` as part of its launch handshake. The regression
accepts the normal entry event, sends `continue`, and then requires the source
breakpoint. Breakpoints, stepping, stack inspection, and variable requests all
flow through unmodified debugpy.

## Runtime defects resolved by the integration

- Monitoring callback values are retained while callbacks run. A blocking
  callback can release the VM execution lock while another debugger thread
  replaces the registered callback without invalidating the active call.
- Code objects use structural equality and matching hashes, allowing debugpy's
  code metadata caches and local monitoring registrations to find equivalent
  code objects. Monitoring callbacks also receive one stable code object per
  compiled function, matching Python's code-object identity contract.
- `_thread._get_main_thread_ident()` remains the creator thread even when
  queried from a debugger worker, and `sys._current_frames()` includes the main
  interpreter frame.
- Live frame refresh is owned by the executing thread. Debugger workers do not
  dereference another thread's transient VM frame-view storage.
- Frame `f_builtins` references the runtime's live builtins namespace instead
  of copying it on every frame lookup.
- Blocking locks, events, sockets, sleeps, joins, and import-lock waits release
  the VM execution lock so debugpy service threads can make progress.
- Monitoring event availability uses a cached aggregate mask instead of scanning
- Each live frame caches its effective global-plus-local monitoring mask by
  configuration generation. Frames without LINE or INSTRUCTION events skip
  source-location monitoring work.
- Live-frame instruction refresh no longer rebuilds every tracked frame's
  locals dictionary. Locals are materialized when `f_locals` or traceback
  locals are requested. This removed roughly 12.8 million unnecessary
  dictionary constructions from the measured DAP lifecycle while preserving
  live `f_locals` behavior.
- `_thread.start_joinable_thread()` and `_thread.start_new_thread()` accept
  general Python callable instances and dispatch them through the runtime's
  ordinary callable protocol. This supports debugpy's asynchronous Pause
  worker without a debugger-specific runtime path.
- The VM hands execution to another thread on a periodic opcode quantum and on
  blocking operations, rather than at every Python function call and return.
  The current regression reaches the breakpoint in 14.6 seconds, completes
  stack/scopes/variables inspection by 14.9 seconds, and exits by 15.7 seconds.
  Before locals-refresh decoupling, the corresponding measurements were 98.4,
  203.1, and 253.6 seconds. The harness may wait another 10 seconds before
  terminating a debugpy adapter that remains in its normal session wait loop.
  Timestamped debugpy logs attribute the remaining cold-start cost mainly to
  launching the two additional XLang3 processes and `pydevd.settrace()` setup.

## Boundary rule

All debugpy, pydevd, and Python standard-library code remains source-backed and
unmodified. Native code supplies XlangVM behavior and native module
requirements. No public C++ replacement for a pure Python library module is
part of this implementation.
