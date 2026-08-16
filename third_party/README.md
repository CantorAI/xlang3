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
# Third-Party Sources

XLang3 vendors selected third-party source dependencies directly so the project can build without requiring package-manager setup on target machines.

## Vendored Libraries

- `nlohmann_json/`
  - Version: `v3.12.0`
  - Source: `https://github.com/nlohmann/json`
  - License: MIT
  - Intended use: builtin `json` module implementation only.

- `yaml-cpp/`
  - Version: `yaml-cpp-0.9.0`
  - Source: `https://github.com/jbeder/yaml-cpp`
  - License: MIT
  - Intended use: native `xlang_yaml` package implementation only.

## Boundary Rule

Third-party C++ types must not cross the XLang3 public ABI. Public APIs continue to use `X3Value`, opaque handles, and C function tables.
