# Native `rpds` package

This package exposes `rpds.rpds` through the XLang3 package ABI. Its persistent
map, set, and list storage comes from the upstream Rust `rpds` 1.2.1 crate,
with `archery` 1.2.2. The C++ file only adapts XLang3 values and Python
protocol calls to the Rust bridge; it does not load PyO3 or a CPython binary.

Cargo is required to build this module. CMake finds it on `PATH`, or accepts
`-DXLANG3_RPDS_CARGO=<absolute path to cargo>`. The locked Rust dependencies
are built in the CMake binary directory. Without Cargo, CMake reports that
the optional native package was disabled; a complete FastAPI compatibility
build requires this package.

The CPython 3.14 comparison is `tests/fastapi/rpds_contract.py`. The current
bridge covers the operations in that oracle, not the entire `rpds-py` API.
`Queue`, hashing, rich comparison, set algebra, pickling, and several
constructor and keyword forms still need parity work.
