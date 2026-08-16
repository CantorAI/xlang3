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
# Native Extension Spec

Status: draft 1

## Purpose

XLang3 should preserve the convenient XPackage development experience while replacing the ABI with a compiler-neutral C ABI.

## Design Summary

```text
Developer experience: XPackage-like, friendly C++ helpers
Runtime contract: pure C ABI
```

## Module Init

Every native package exports:

```c
X3_EXPORT X3Status x3_package_init(
    const X3PackageHost* host,
    X3Package* package);
```

The runtime loads this symbol by exact unmangled name.

## Package And Module Builder

A native package is the ABI/loading unit. The runtime exposes package contents as normal `ModuleObject` values.

One native package may register several modules. This keeps the useful old XLang pattern where one DLL, such as `xlang_http`, exposes `http`, `cypher`, and `smtp`.

```c
X3Module* cypher = NULL;
host->add_module(package, "cypher", &cypher);
host->module_add_value(cypher, "RSA_PKCS1_PADDING", x3_value_int64(1));
```

The module builder registers:

- functions
- constants
- types/classes
- properties
- events later
- submodules

Example C API:

```c
typedef struct X3NativeFunctionDef {
    uint32_t size;
    const char* name;
    X3NativeFn callback;
    void* user_data;
    uint32_t min_argc;
    uint32_t max_argc;
    uint32_t flags;
} X3NativeFunctionDef;

X3Status module_add_function(X3Module* module, const X3NativeFunctionDef* def);
```

Package-wide state is owned by the package but cleaned up by the runtime:

```c
host->package_set_cleanup(package, state, cleanup_state);
```

The cleanup callback must release any retained `X3Value` handles and free package-owned memory. The runtime runs package cleanup after module values are released, while the native package binary is still loaded.

## C Native Function Example

```c
static X3Status add(
    X3CallContext* ctx,
    X3Runtime* runtime,
    void* user_data,
    const X3Value* args,
    uint32_t argc,
    X3Value* out)
{
    (void)runtime;
    (void)user_data;
    if (argc != 2) return X3_STATUS_ERROR;
    if (args[0].tag == X3_TAG_INT64 && args[1].tag == X3_TAG_INT64) {
        *out = x3_value_int64(args[0].as.i64 + args[1].as.i64);
        return X3_STATUS_OK;
    }
    return X3_STATUS_ERROR;
}
```

## C++ Helper

Provide helper headers under:

```text
include/xlang3/cpp/
```

The first compatibility wrapper is `include/xlang3/cpp/xlang.h`. It supports the old embedding shape:

```cpp
X::Runtime rt;
rt.AddImportRoot(package_dir);

X::Package cypher(rt, "cypher", "xlang_http");
cypher.SetPropValue("StorePath", "CantorStore");
auto generateKeyPair = cypher.fn("generate_key_pair");
auto key = generateKeyPair(2048, keyName);
```

Package-author helpers should later allow old XPackage-like registration:

```cpp
class MyMath {
public:
    int add(int a, int b);
};

X3_BEGIN_PACKAGE(MyMath)
    X3_ADD_METHOD("add", &MyMath::add)
X3_END_PACKAGE()
```

But this helper must generate C ABI registration and C ABI trampoline functions.

No C++ class pointer should be required by the runtime unless wrapped as opaque package data.

## Runtime Import Semantics

Native package loading participates in normal import:

```text
runtime module table
Python .py module/package
native package DLL/SO
```

Native package filenames:

```text
Windows: <name>.x3pkg.dll, <name>.dll
Linux:   lib<name>.x3pkg.so, <name>.x3pkg.so, lib<name>.so
macOS:   lib<name>.x3pkg.dylib, <name>.x3pkg.dylib, lib<name>.dylib
```

For requested module name `M`, the native loader uses filename-based probing:

```text
1. M.x3pkg.<platform-extension>
2. xlang_M.x3pkg.<platform-extension>
```

The `xlang_` fallback lets a package keep an XLang-native package filename while supporting Python-compatible imports. For example:

```python
import sqlite3
```

may load:

```text
xlang_sqlite3.x3pkg.dll
```

No manifest scan is required, and the loader must not load unrelated DLLs just to ask what they provide.

Language code sees a normal module object. C++ embedding code uses the same runtime module object through the compatibility wrapper.

## Type Registration

Native extension types register `X3TypeDef`:

```c
typedef struct X3TypeDef {
    const char* name;
    uint32_t size;
    X3TypeSlots slots;
    X3MemberDef* members;
    uint32_t member_count;
} X3TypeDef;
```

The runtime creates `X3Type`.

## Compiler Compatibility

Native extensions should work when runtime is built by MSVC and extension by:

- MSVC
- Clang
- GCC
- MinGW
- Rust
- Zig
- C
- C++

This requires:

- no STL across ABI
- no exceptions across ABI
- no RTTI across ABI
- no virtual interfaces across ABI

## Package Loading

Search order:

1. built-in statically registered modules
2. Python module search path
3. native package filenames rooted in runtime import roots

Package metadata files are optional documentation/build metadata only. They are not required for import resolution.

## Retained From XPackage

Retain:

- easy function registration
- property registration
- class/type registration
- package object identity
- package cleanup hook
- wait/async hook later
- embedding native object pointer

Replace:

- C++ virtual package ABI
- dynamic_cast across runtime boundary
- STL-based ABI types
