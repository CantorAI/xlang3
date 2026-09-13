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
# Visual Studio 2026 Python Debug Smoke

`vs2026_launch.json` uses Visual Studio 2026's installed Python debugger in the
same adapter, launcher, and debuggee arrangement used for CPython:

```text
Visual Studio Debug Adapter Host
  -> xlang3.exe running Visual Studio's debugpy adapter
    -> xlang3.exe running Visual Studio's debugpy launcher
      -> xlang3.exe running the debug server and user program on XlangVM
```

The adapter and pydevd sources are Visual Studio's installed, unmodified files.
Every interpreter process is `xlang3.exe`; the profile does not use a
`python.exe` alias or an installed CPython runtime. XLang3 executes the debug
server and target program as ordinary Python code. `PYDEVD_USE_SYS_MONITORING=1`
selects the Python 3.14 monitoring backend.

## Normal F5 workflow

For the same environment picker used by CPython, first register the Release
output once in **View > Other Windows > Python Environments**. Choose
**Add Environment > Existing environment > Custom** and use:

```text
Description:       XLang3 3.14
Prefix path:       D:\CantorAI\xlang3\build\Release
Interpreter path:  D:\CantorAI\xlang3\build\Release\xlang3.exe
Library path:      <XLang3 install>\lib\python3.14
Language version:  3.14
Architecture:      64-bit
Path variable:     PYTHONPATH
```

Select **XLang3 3.14** in Visual Studio's Python Environment dropdown.

Open `D:\CantorAI\xlang3` with **File > Open > Folder**. In the toolbar's
**Startup Item** dropdown, select **XLang3 Python 3.14 (debugpy)**. Open
`tests/ide/vs2026_debug_smoke.py`, set a breakpoint on line 17, and press F5.
The repository-root `launch.vs.json` provides this selectable profile.

When execution stops, hovering over `value` on line 16 should display `41`.
The same expression should return `41` in **Debug > Windows > Watch > Watch 1**.
If Watch works but the editor shows no DataTip, confirm that the active editor
is the Python editor and restart Visual Studio after reopening the repository
folder. DataTips are available only while the debugger is stopped. The
automated launch regression verifies that debugpy advertises hover support and
that a DAP `evaluate` request with `context: "hover"` returns `41`.

If the profile is hidden, open **Show/Hide Debug Targets** from the Startup Item
dropdown and enable it. Visual Studio also reloads root launch profiles when the
folder is reopened.

## Direct adapter fallback

To launch the lower-level adapter configuration directly, use Visual Studio's
Command Window:

```text
DebugAdapterHost.Logging /On /OutputWindow
DebugAdapterHost.Launch /LaunchJson:"D:\CantorAI\xlang3\tests\ide\vs2026_launch.json"
```

Set a breakpoint on line 17 of `vs2026_debug_smoke.py`. Visual Studio should
stop with local variable `value` equal to `41`; continuing prints `42` and the
session exits cleanly.

The deterministic protocol regression loads this JSON file, starts the adapter
entry declared by `$adapter` and `$adapterArgs`, and derives its DAP launch
request from the same configuration:

The CTest target is `xlang3_cli_visual_studio_debugpy_launch`.

The checked-in paths describe this repository's Windows test machine. Update
the Visual Studio edition path for another machine.
