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
# XLang3 VS Code Debug Adapter Registration

This folder contains a minimal VS Code extension that registers the `xlang3`
debug type and starts:

```text
xlang3 --dap-stdio
```

For local development, set one of:

```json
"xlang3.debugAdapterPath": "D:/CantorAI/xlang3/build/Release/xlang3.exe"
```

or:

```json
"adapterPath": "D:/CantorAI/xlang3/build/Release/xlang3.exe"
```

inside a launch configuration.

Example `.vscode/launch.json`:

```json
{
  "version": "0.2.0",
  "configurations": [
    {
      "name": "XLang3: Current Python File",
      "type": "xlang3",
      "request": "launch",
      "program": "${file}",
      "adapterPath": "D:/CantorAI/xlang3/build/Release/xlang3.exe",
      "args": []
    }
  ]
}
```
