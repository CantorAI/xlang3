# XLang3

XLang3 is a redesign of XLang focused on a cleaner Python-compatible language front end, a compact universal value runtime, and an execution pipeline that can support multiple executors.

The core runtime direction stays close to XLang: `X::Value` remains the universal value representation, scalar values are stored directly, complex values are represented as XLang objects, and memory management is based on the XLang reference-counted object model rather than a tracing GC or CPython `PyObject*` runtime.

The major architectural change is to separate syntax, semantic binding, execution IR, and runtime execution:

```text
source -> parser -> AST -> semantic model -> program IR / graph IR -> executors
```

The first executors target a direct interpreter and an optimized interpreter/JIT path. Native compilation can be added later without making LLVM or any compiler toolchain a required dependency for normal runtime deployment.

This repository currently contains the initial implementation specifications for the XLang3 design.
