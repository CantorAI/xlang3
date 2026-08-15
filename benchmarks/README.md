# XLang3 Benchmarks

Benchmarks compare XLang3 against CPython on the same pure Python source files.

Initial goals:

- keep benchmark sources small and readable
- compare interpreter-only XLang3 first
- record command lines, Python version, compiler, CPU, and build mode
- avoid benchmarking startup cost unless the benchmark is explicitly about startup

Planned groups:

- `scalar_arithmetic`: integer and floating-point operator dispatch
- `local_slots`: local variable load/store and loop behavior
- `function_calls`: direct function calls and argument passing
- `branches`: if/else and comparison dispatch
- `containers`: list/dict once container runtime exists

XLang3 runs `.py` files directly. Benchmark files should therefore be valid Python files that can run on both CPython and XLang3 whenever the implemented language subset allows it.

The first milestone is not to beat CPython everywhere. It is to make interpreter performance visible early, especially where `X3Value` scalar fast paths and sema-assigned local slots should help.
