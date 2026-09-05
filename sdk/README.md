# XLang3 SDK Headers

Add the SDK include directory to the compiler include path:

```text
<xlang3-sdk>/sdk
```

Use the same entry header from C or C++:

```c
#include "xlang3/xlang3.h"
```

## Calls and Streams

An executable can register a C++ service using the same in-class package API
as a native DLL:

```cpp
Service service; // Defines BEGIN_PACKAGE / APISET().Add* / END_PACKAGE.
X::Runtime runtime;
auto module = runtime.RegisterPackage("service", service);
```

Registration borrows the service; it must outlive the runtime. The runtime owns
the module bindings, not the service. Duplicate names are rejected. Release
runtime-backed values stored inside a service before runtime destruction.

C uses `x3_call_kw`, `x3_value_to_stream`, and `x3_value_from_stream`.
C++ uses the same ABI through `X::Value` and `X::Stream`:

```cpp
X::Value result;
bool called = function.Call(args, {{"offset", X::Value(runtime, 2)}}, result);

X::Stream stream(runtime);
bool written = result.ToBytes(stream);
bool rewound = written && stream.Rewind();
X::Value restored;
bool read = rewound && restored.FromBytes(stream);
```

Streams support runtime-owned blocks, borrowed read-only blocks, and caller-allocated
output blocks. Borrowed buffers and allocator context must outlive their stream;
the runtime and host must outlive all associated streams and values. Streams are
single-owner, not concurrently accessible. Rewind switches an output stream to
reading; a failed stream must be discarded.

Standalone serialization preserves shared references and cycles in lists, tuples,
dictionaries, script functions, classes, and instances. Functions carry executable
IR, closures, defaults, and their globals snapshot. Native instances require a
registered `X3NativeSerializerDef`; process pointers are never serialized.

Deserialize only trusted data: restoring a graph can import native modules and
invoke registered callbacks, and restored functions can execute code. This is not
a sandbox or a CPython pickle format. See [serialization](../doc/serialization.md)
for supported types, dependencies, and lifetime rules.

Native C++ methods accepting `const X::ARGS&` and `const X::KWARGS&` can be
registered with `AddVarFunc`; keyword values retain object identity.
`AddPropL(name, setter, getter)` registers a writable native-class property.
Use `Value::SetAttr` for attribute assignment and `Value::Set` for dictionary keys.

ABI version 21 requires rebuilding native packages.
