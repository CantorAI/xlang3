These Windows x86-64 libffi sources come from the bundled
`src/c/libffi_x86_x64` directory of the upstream `python-cffi/cffi` source
tree at commit `8a97fd4`. They are native C and assembly code and do not
load or link against CPython. The only local change in `ffi.c` replaces three
calls to CPython's process-terminating `Py_FatalError` with standard C
`abort()` for libffi internal invariant failures. The original MIT-style
license is included in `LICENSE`.
