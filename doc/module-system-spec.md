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
# Module System Spec

Status: draft 0

## Purpose

Separate core runtime from standard/builtin modules.

XLang3 modules should be independently buildable and optionally loadable.

## Module Categories

```text
core runtime modules:
  required by language/runtime

loadable modules:
  json, text, yaml, http, os, time, sqlite, net, image, tensor

builtin modules:
  _builtins, math

third-party modules:
  loaded via package search path
```

## Directory Layout

```text
modules/yaml/
  yaml_package.cpp
  yaml_convert.cpp
  yaml_convert.h
  CMakeLists.txt

modules/json/
  json_package.cpp
  json_convert.cpp
  json_convert.h
  CMakeLists.txt
```

## Builtin vs Loadable

Each module should support two modes when possible:

```text
static: linked into runtime/application
dynamic: loaded as shared library
```

The module registration ABI should be the same in both cases.

## Import Model

Python-compatible import syntax lowers to XLang3 import operations.

Examples:

```python
import json
from xlang_yaml import yaml
```

IR should represent import explicitly:

```text
ImportModule dst, module_name
ImportFrom dst, module_name, symbol_name
```

Import runtime resolves:

1. already loaded module cache
2. built-in module registry
3. source module loader
4. dynamic package loader by filename

Native package lookup is filename-based. For requested module `M`, the loader tries:

```text
M.x3pkg.dll
xlang_M.x3pkg.dll
```

with platform-specific extensions on non-Windows systems. This supports Python-compatible imports without manifest scanning:

```python
import sqlite3
```

can load:

```text
xlang_sqlite3.x3pkg.dll
```

The default runtime layout prefers subfolders under the runtime library location:

```text
lib/
modules/
site-packages/
```

The runtime also searches the runtime library folder itself after those subfolders, so a simple deployment can still place package files beside `xlang3_runtime.dll` or the CLI executable. Source file folders are prepended by the CLI/eval-file path so local application modules keep Python-like precedence.

## Module Object

Module object is an XLang object with:

- name
- path
- globals slots
- version
- attribute access by symbol table/cache

Globals should be indexed slots after binding, not repeated string lookup in hot code.

## Standard Module Split

Do not mix these into `core`:

```text
http
text
yaml
sqlite
image
net
tensor
```

`json` is a loadable native package, not part of the runtime core. Runtime stringification for dict/list/repr stays in the value/object layer and does not depend on JSON.

Core may provide primitive services only:

- memory
- values
- object/type
- modules/import registry
- package loading
- executor
- errors

## Source Modules

`.py` source modules parse and lower to IR directly.

Compiled module cache may store serialized IR.

## Native Modules

Native modules expose C ABI init function.

Native modules may provide:

- package object
- module-level functions
- native classes/types
- constants
