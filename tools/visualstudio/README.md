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
# Visual Studio Native Debug Launcher

This launcher starts XLang3's native DAP debugger in Visual Studio:

```text
xlang3.exe --dap-stdio
```

It does not use Visual Studio's normal Python/debugpy path.

Example:

```powershell
powershell -ExecutionPolicy Bypass -File D:\CantorAI\xlang3\tools\visualstudio\xlang3-vs-debug.ps1 D:\CantorAI\xlang3\tests\ide\vs2026_debug_smoke.py
```

The script creates a temporary Visual Studio Debug Adapter Host launch JSON and
sends:

```text
DebugAdapterHost.Launch /LaunchJson:"..."
```

Normal Visual Studio Python Environment debugging still belongs to Microsoft's
Python/debugpy integration. XLang3 native debugging needs the native DAP adapter
path until a VSIX/product registration is added.
