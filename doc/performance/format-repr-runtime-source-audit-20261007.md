# Runtime repr formatting source audit â€” 2026-10-07

After the native AST defaults checkpoint, official Chameleon fails while compiling its generated token map. Chameleon's Python compiler formats `(Token, line, column)` with `%r`. A small probe on CPython 3.14.7 and XLang3 isolates the defect: `repr(tuple)` preserves quotes around the str-subclass payload, but XLang3's `%r` renderer emits the nested token without quotes. Direct `%r` on the subclass succeeds. This is a generic container conversion defect, not a Chameleon algorithm defect.

## Native reference and XLang3 cause

[CPython 3.14.7 unicode formatting](https://github.com/python/cpython/blob/v3.14.7/Objects/unicodeobject.c) uses normal object repr for `%r` and ASCII repr for `%a`. [Bytes formatting](https://github.com/python/cpython/blob/v3.14.7/Objects/bytesobject.c) uses ASCII repr for both conversions. [Object repr](https://github.com/python/cpython/blob/v3.14.7/Objects/object.c) dispatches through the type's repr slot and verifies a string result. These are native core language operations.

In `src/runtime/value.cpp`, `string_percent_format` previously invoked runtime-aware repr only when the outer argument was an InstanceObject. Native tuple/list/dict arguments went to `value_to_repr`, which has no Runtime and cannot execute Python element repr methods. Bytes repr formatting unconditionally used the same runtime-free renderer. The `.format()` conversion path in `string_methods.cpp` had the same outer-tag assumption.

The candidate exposes the existing intrinsic builtin repr/ASCII implementation through `runtime_repr`, then calls it from all three formatting conversion paths. It shares recursion handling and Python type dispatch, without searching a rebound builtin name or emitting an extra synthetic profiling event. Repr lookup on Python instances uses their type's special-method lookup, so instance attributes and `__getattribute__` do not replace the slot.

The first XLang3 fixture run additionally exposed a slow-path exception bug: modulo formatting changed the expected TypeError into RuntimeError. Both VM modulo variants now preserve a pending Python exception before synthesizing a formatting error. This check is only on failure; immediate numeric modulo remains unchanged.

## Validation scope

The fixture checks native containers containing Python objects, string subclasses and custom repr, `%r`/`%a` on str and bytes, `.format()` conversions, ASCII escaping before precision, special-slot lookup, rebound builtin names, recursive lists, invalid repr results and user exceptions. The fixture passes CPython 3.14.7. Candidate correctness, complete fixed gate and official Chameleon evidence must pass or retain their failure status before an engine commit.

This work does not translate any CPython pure-Python library algorithm to C++. It also does not claim that a failed workload has a timing score or that the full-suite goal is complete.
