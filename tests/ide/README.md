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
# Visual Studio IDE Debug Smoke

`vs2026_launch.json` is a Visual Studio Debug Adapter Host launch file for
testing XLang3 in Visual Studio 2026.

It uses Visual Studio's special Debug Adapter Host fields:

```json
{
  "$adapter": "D:\\CantorAI\\xlang3\\build\\Release\\xlang3.exe",
  "$adapterArgs": "--dap-stdio"
}
```

The smoke target is:

```text
tests/ide/vs2026_debug_smoke.py
```

The launch file sets `stopAtEntry: true`, so Visual Studio should stop at the
first executable line without needing a manually placed editor breakpoint.

Manual VS command-window flow:

```text
DebugAdapterHost.Logging /On /OutputWindow
DebugAdapterHost.Launch /LaunchJson:"D:\CantorAI\xlang3\tests\ide\vs2026_launch.json"
```

Expected result:

- Visual Studio launches `xlang3.exe --dap-stdio`.
- The debugger stops on the first line.
- Continue runs the program.
- The adapter exits cleanly after the DAP `terminated` event.
