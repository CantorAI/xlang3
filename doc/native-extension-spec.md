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

Status: draft 0

## Purpose

XLang3 should preserve the convenient XPackage development experience while replacing the ABI with a compiler-neutral C ABI.

## Design Summary

```text
Developer experience: XPackage-like, friendly C++ helpers
Runtime contract: pure C ABI
```

## Module Init

Every native extension exports:

```c
X3_EXPORT X3Status xlang3_module_init(
    const X3Api* api,
    X3Runtime* rt,
    X3PackageBuilder* builder);
```

The runtime loads this symbol by exact unmangled name.

## Package Builder

The builder registers:

- functions
- constants
- types/classes
- properties
- events later
- submodules

Example C API:

```c
X3Status x3_package_add_func(
    X3PackageBuilder* b,
    const char* name,
    X3NativeFunc fn,
    const X3FuncSig* sig);
```

## C Native Function Example

```c
static X3Status add(
    X3Runtime* rt,
    X3Value self,
    const X3Value* args,
    uint32_t argc,
    const X3KwArg* kwargs,
    uint32_t kwargc,
    X3Value* out)
{
    if (argc != 2) return X3_ERR_TYPE;
    if (args[0].tag == X3_TAG_INT64 && args[1].tag == X3_TAG_INT64) {
        *out = x3_make_int(args[0].as.i64 + args[1].as.i64);
        return X3_OK;
    }
    return x3_binary_add(rt, args[0], args[1], out);
}
```

## C++ Helper

Provide helper headers under:

```text
include/xlang3/cpp/
```

C++ helper may allow:

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

1. built-in statically registered packages
2. module search path
3. configured package folders
4. application-local modules

Each package may have metadata:

```json
{
  "name": "std.yaml",
  "abi": 1,
  "version": "0.1.0",
  "entry": "xlang3_module_init"
}
```

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

