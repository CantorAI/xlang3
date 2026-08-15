# Runtime C ABI Spec

Status: draft 0

Purpose: define the stable compiler-neutral boundary for XLang3 runtime, modules, and native extensions.

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
X3Status xlang3_module_init(const X3Api* api, X3Runtime* rt, X3PackageBuilder* builder);
```

The runtime never calls C++ methods across the boundary. It calls C function pointers registered through `X3Api`.

## Core Opaque Types

```c
typedef struct X3Runtime X3Runtime;
typedef struct X3Object X3Object;
typedef struct X3Type X3Type;
typedef struct X3Module X3Module;
typedef struct X3Package X3Package;
typedef struct X3PackageBuilder X3PackageBuilder;
typedef struct X3Executor X3Executor;
```

## Status

```c
typedef enum X3Status {
    X3_OK = 0,
    X3_ERR = 1,
    X3_ERR_TYPE = 2,
    X3_ERR_INDEX = 3,
    X3_ERR_KEY = 4,
    X3_ERR_IMPORT = 5,
    X3_ERR_UNSUPPORTED = 6,
    X3_ERR_NOMEM = 7
} X3Status;
```

Errors should be stored on `X3Runtime` as structured exception values. `X3Status` is for fast C control flow.

## API Table

Extensions receive a versioned API table:

```c
typedef struct X3Api {
    uint32_t abi_version;
    uint32_t size;

    X3Value (*make_none)(void);
    X3Value (*make_bool)(int value);
    X3Value (*make_int)(int64_t value);
    X3Value (*make_double)(double value);
    X3Status (*make_string)(X3Runtime* rt, const char* data, size_t len, X3Value* out);

    void (*value_inc_ref)(X3Value* value);
    void (*value_dec_ref)(X3Value* value);

    X3Status (*get_attr)(X3Runtime* rt, X3Value self, const char* name, size_t len, X3Value* out);
    X3Status (*set_attr)(X3Runtime* rt, X3Value self, const char* name, size_t len, X3Value value);
    X3Status (*call)(X3Runtime* rt, X3Value callee, const X3Value* args, uint32_t argc, X3Value* out);

    X3Status (*package_add_func)(X3PackageBuilder* b, const char* name, X3NativeFunc fn, const X3FuncSig* sig);
    X3Status (*package_add_value)(X3PackageBuilder* b, const char* name, X3Value value);
    X3Status (*package_add_type)(X3PackageBuilder* b, const X3TypeDef* type_def);
} X3Api;
```

`abi_version` and `size` allow backward-compatible extension.

## Native Function

```c
typedef X3Status (*X3NativeFunc)(
    X3Runtime* rt,
    X3Value self,
    const X3Value* args,
    uint32_t argc,
    const X3KwArg* kwargs,
    uint32_t kwargc,
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

