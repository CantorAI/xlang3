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
# XLang3 Agent Tooling

This folder contains Codex-driven development tooling for this repository.

`agent/config.toml` records the shared XLang3 layout: build folder, Release
executable, fixture root, and registered goal packages.

`agent/scripts` holds shared Python runners that read `agent/config.toml` and
know the XLang3 checkout, Visual Studio CMake build, fixture layout, audit
documents, and commit flow.

Goal folders such as `agent/python314_compat` hold the durable instructions,
queue, state, and skill metadata for one long-running product goal.

Useful commands:

```text
C:\Python\Python314\python.exe agent\scripts\codex_loop.py --goal python314_compat --status
C:\Python\Python314\python.exe agent\scripts\codex_loop.py --goal python314_compat --dry-run
C:\Python\Python314\python.exe agent\scripts\run_fixtures.py
```

PowerShell wrapper:

```text
powershell -ExecutionPolicy Bypass -File agent\run_python314_compat.ps1 -Status
powershell -ExecutionPolicy Bypass -File agent\run_python314_compat.ps1 -Section "Standard Modules Foundation" -DryRun
```
