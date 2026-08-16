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
# Runtime C ABI Spec

Status: draft 0

Purpose: define the stable compiler-neutral boundary for XLang3 runtime, modules, and native extensions.

## Build Artifacts

XLang3 separates the runtime engine from the command-line frontend:

```text
xlang3_runtime.dll / libxlang3_runtime.so
  shared runtime engine and C ABI exports

xlang3_runtime_static.lib / libxlang3_runtime_static.a
  static runtime engine for embedders that do not want a runtime DLL/SO

xlang3.exe
  CLI only; links to xlang3_runtime
```

Native packages should use the C ABI and host table. They should not link against internal C++ runtime headers or depend on runtime C++ object layouts.

## ABI Rule

The runtime ABI must be C-compatible.

Allowed across ABI boundary:

- integers
- floats
- pointers to opaque structs
- plain structs with fixed-size fields
- function pointers
- `const char*` plus explicit length
- `X3Value`
- `X3Status`

Forbidden across ABI boundary:

- C++ classes
- virtual interfaces
- `std::string`
- `std::vector`
- exceptions
- RTTI objects
- templates
- compiler-specific name mangling

## Export Convention

Every dynamic module exports:

```c
extern "C" X3_EXPORT
X3Status x3_package_init(const X3PackageHost* host, X3Package* package);
```

The runtime never calls C++ methods across the boundary. It calls C function pointers registered through `X3PackageHost`.

## Core Opaque Types

```c
typedef struct X3Runtime X3Runtime;
typedef struct X3Object X3Object;
typedef struct X3Type X3Type;
typedef struct X3Module X3Module;
typedef struct X3Package X3Package;
typedef struct X3Executor X3Executor;
```

## Status

```c
typedef enum X3Status {
    X3_STATUS_OK = 0,
    X3_STATUS_ERROR = 1
} X3Status;
```

Errors should be stored on `X3Runtime` as structured exception values. `X3Status` is for fast C control flow.

## Package Host Table

Extensions receive a versioned API table:

```c
typedef struct X3PackageHost {
    uint32_t abi_version;
    uint32_t size;

    X3Status (*add_module)(X3Package* package, const char* name, X3Module** out_module);
    X3Status (*module_add_value)(X3Module* module, const char* name, X3Value value);
    X3Status (*module_add_function)(X3Module* module, const X3NativeFunctionDef* def);
    X3Status (*set_error)(X3CallContext* context, const char* message);

    const char* (*runtime_last_error)(X3Runtime* runtime);
    void (*value_release)(X3Value value);
    X3Value (*value_string)(X3Runtime* runtime, const char* value);
    X3Value (*value_list)(X3Runtime* runtime);
    X3Value (*value_dict)(X3Runtime* runtime);
    const char* (*value_to_cstr)(X3Runtime* runtime, X3Value value);
    X3ObjectKind (*value_object_kind)(X3Value value);
    X3Status (*len)(X3Runtime* runtime, X3Value value, uint64_t* result);
    X3Status (*get_item)(X3Runtime* runtime, X3Value object, X3Value key, X3Value* result);
    X3Status (*list_append)(X3Runtime* runtime, X3Value list, X3Value item);
    X3Status (*dict_set_item)(X3Runtime* runtime, X3Value dict, X3Value key, X3Value item);
    X3Status (*dict_get_entry)(X3Runtime* runtime, X3Value dict, uint64_t index, X3Value* key, X3Value* value);
} X3PackageHost;
```

`abi_version` and `size` allow backward-compatible extension.

## Native Function

```c
typedef X3Status (*X3NativeFn)(
    X3CallContext* context,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* out);
```

Native functions must not throw C++ exceptions across the ABI.

## Object Contract

Runtime objects begin with a common header:

```c
typedef struct X3ObjectHeader {
    X3Type* type;
    uint32_t refcnt;
    uint32_t flags;
} X3ObjectHeader;
```

Concrete objects may be implemented in C++ internally:

```cpp
struct X3Dict {
    X3ObjectHeader header;
    std::unordered_map<X3Value, X3Value, X3ValueHash> map;
};
```

But dispatch must use type slots, not C++ virtual methods.

## Type Slots

```c
typedef struct X3TypeSlots {
    X3UnaryFn truth;
    X3UnaryFn len;
    X3BinaryFn add;
    X3BinaryFn sub;
    X3BinaryFn mul;
    X3BinaryFn div;
    X3GetAttrFn get_attr;
    X3SetAttrFn set_attr;
    X3GetItemFn get_item;
    X3SetItemFn set_item;
    X3CallFn call;
    X3IterFn iter;
    X3NextFn next;
    X3DestroyFn destroy;
    X3HashFn hash;
    X3CompareFn compare;
} X3TypeSlots;
```

Executor fast paths may compare `obj->type` directly and call known slot functions directly.

## Refcount Rules

Use explicit ownership annotations in implementation comments:

- `borrowed`: caller does not own a reference
- `new`: caller owns a reference
- `stolen`: callee consumes caller reference

Default C ABI rule:

- input `X3Value` arguments are borrowed
- output `X3Value* out` returns a new owned value
- storing a value increments/retains as needed
- destroying a frame decrements stored owned values

## ABI Versioning

Every module should declare:

```c
#define X3_ABI_VERSION 1
```

Runtime rejects modules with incompatible major ABI version.
