# Module System Spec

Status: draft 0

## Purpose

Separate core runtime from standard/builtin modules.

XLang3 modules should be independently buildable and optionally loadable.

## Module Categories

```text
core runtime modules:
  required by language/runtime

std modules:
  text, json, yaml, http, os, time, sqlite, net, image, tensor

third-party modules:
  loaded via package search path
```

## Directory Layout

```text
modules/std/yaml/
  include/
  src/
  tests/
  xlang3_module.json
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
import std.yaml as yaml
from std.http import get
```

IR should represent import explicitly:

```text
ImportModule dst, module_name
ImportFrom dst, module_name, symbol_name
```

Import runtime resolves:

1. already loaded module cache
2. built-in module registry
3. dynamic package loader
4. source module loader

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

Core may provide primitive services only:

- memory
- values
- object/type
- modules/import registry
- package loading
- executor
- errors

## Module ABI Metadata

```json
{
  "name": "std.yaml",
  "version": "0.1.0",
  "abi": 1,
  "kind": "native",
  "entry": "xlang3_module_init",
  "dependencies": ["std.text"]
}
```

## Source Modules

`.py` and future `.x3` source modules parse and lower to IR.

Compiled module cache may store serialized IR.

## Native Modules

Native modules expose C ABI init function.

Native modules may provide:

- package object
- module-level functions
- native classes/types
- constants

