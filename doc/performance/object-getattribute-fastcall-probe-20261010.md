# `object.__getattribute__` fast-call probe (2026-10-10)

## Question

Does registering the existing generic fast-call adapter for
`object.__getattribute__` reduce the repeated call cost in CPython's unchanged
`inspect.getattr_static` implementation, which is exercised by
`typing_runtime_protocols`?

## Change and result

`src/builtins/object_type_builtins.cpp` now registers
`object.__getattribute__` with `builtin_fast_adapter`. This keeps the ordinary
two-positional-argument builtin call on the stack-backed fast-call path; it
does not replace or specialize the pure-Python `inspect` or `typing` code.

The fixed Release build completed and the fixture suite passed. A 5,000-call
diagnostic run compared the candidate binary with its immediate parent and
CPython 3.14.7. The diagnostic uses the official pyperformance benchmark's
`ManyInstanceVars` class and CPython's unchanged `inspect.py`; it includes
Python lambda/call overhead and is not an official pyperformance score.

| Operation | Candidate | Parent | Candidate / parent | CPython 3.14.7 | Candidate / CPython |
| --- | ---: | ---: | ---: | ---: | ---: |
| `object.__getattribute__(instance, "__dict__")` | 4.867 ms | 5.665 ms | 0.859x | 0.342 ms | 14.2x |
| `inspect.getattr_static(instance, "a")` | 106.322 ms | 107.536 ms | 0.989x | 6.438 ms | 16.5x |
| `inspect.getattr_static(instance, "missing", None)` | 108.272 ms | 108.801 ms | 0.995x | 5.677 ms | 19.1x |
| `inspect._check_class(klass, "a")` | 53.468 ms | 53.540 ms | 0.999x | 3.375 ms | 15.8x |

The adapter improves its direct builtin call by about 14%, but the complete
static-lookup operation changes by only about 1% and remains 16–19x slower
than CPython. This is not a material pyperformance win. The dominant cost is
spread across the generic Python-to-builtin calls and mapping/dictionary
operations inside the library implementation, so optimizing this one entry
point cannot close the gap.

## Reproduction

From the repository root, run
`benchmarks/diagnostics/profile_inspect_static_lookup.py 5000 <path-to-the-installed-pyperformance-typing-runtime-protocols-run_benchmark.py>`
under each executable. Use `build-repro/main-verify-20261006/Release/xlang3.exe`
for the candidate, `build-repro/weakref-slot-parent-20261010/Release/xlang3.exe`
for the parent, and `C:\Python\Python314\python.exe` for CPython 3.14.7.
These probes identify costs; use the official pyperformance runner to score a
candidate before accepting any optimization.

The broader goal remains active. The next work should target the shared
attribute/dictionary operation path exposed by the profile, while preserving
Python-visible descriptor, subclass, and exception semantics.
