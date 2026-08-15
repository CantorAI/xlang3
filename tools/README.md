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
# XLang3 Tools

This directory is for developer tools used to build, validate, or inspect XLang3.

Initial planned tools:

- `parsergen/`: generate the Python-compatible parser from the checked-in grammar.
- `grammarcheck/`: validate grammar changes and parser test fixtures.
- `irdump/`: print program IR and graph IR in a stable debug format.
- `abicheck/`: validate native extension ABI headers and generated bindings.

Tools should be build-time or developer-time dependencies. The normal XLang3 runtime should not depend on parser generator tooling, LLVM, or other large toolchain packages unless a specific executor mode requires them.
