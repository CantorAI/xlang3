# Native array dispatch: separate subscription from saved-method calls

This source audit follows the committed `23d1d443` subscription checkpoint.
The full all-97 run is still using that frozen executable. This document
records a dispatch distinction that prevents testing the wrong optimization;
it establishes no new timing improvement.

## Current XLang3 paths

The integer array subscription path in
`src/executor/xlang_vm/ops/xlang_vm_ops_containers.h` recognizes an instance
with a native `__getitem__` defined directly on its class. Its cache retains
weak class/method targets, guarded by class identity and version. For the
callback it temporarily owns the native callable and both operands, then
calls `runtime_call_callable`. This avoids constructing a bound method on
every subscription and protects operands when the destination aliases an
input or `__index__` mutates the class.

In `src/runtime/functional_iterators.cpp`, the native branch of
`runtime_call_callable` calls `NativeFunctionObject.callback`. It does not
select `fast_callback`. It retains the generic native monitoring events.
Registering a fast callback for the array getter therefore does not, by
itself, change this hot subscription path.

Saved-method calls instead enter the VM's native-call machinery in
`src/executor/xlang_vm/ops/xlang_vm_ops_call.h`. Its ordinary callback path
materializes arguments and releases/reacquires the execution lock; eligible
fast callbacks can consume the argument view directly and keep the lock.
Those are potential saved-call costs, not evidence about subscriptions.
Slice conversion and Python `__index__` can re-enter Python, so a new fast
callback would need an explicit re-entry/lifetime audit and measurements.

The array scalar reader already uses authoritative native storage and
constant-size loads in `src/runtime/modules/system/array_module.cpp`.
Reintroducing a second mirrored buffer would undo the previous measured
improvement. No additional fast callback is implemented by this audit.

## CPython 3.14.7 reference

CPython's generic [`PyObject_GetItem`](https://github.com/python/cpython/blob/v3.14.7/Objects/abstract.c)
calls the type's mapping subscription slot when available. The native
[`array` implementation](https://github.com/python/cpython/blob/v3.14.7/Modules/arraymodule.c)
registers `array_subscr` in that slot; it converts integer indices, adjusts
negative indices, and delegates to a bounds-checked item reader. The double
reader loads the native element and creates a Python float. This is a
native-module path, so XLang3's own native array implementation is consistent
with the project's pure-Python library rule. This reference does not establish
which specialized bytecode executes in any particular measured CPython loop.

## Next discriminating measurement

The official SciMark `ArrayList.__getitem__` body remains Python. Its integer
row path first executes `isinstance(index, tuple)` before returning a list
row. LU invokes this row access repeatedly, independently of native element
loading. The pending immediate-value `isinstance` guard targets the failed
instance-attribute probe after the complete class-info/hook check.

The extended subscription diagnostic separates primitive false/true type
checks, checked Python row subscription, a saved Python row getter, and raw
list row access. Compare the preserved exact `23d1d443` binary with the next
validated candidate in both executable orders. Inspect the controls before
attributing a gain to the primitive guard. Then run official SciMark LU/all
five subtests, `typing_runtime_protocols`, and Chameleon, preserving failures.
The complete fixed Release gate is mandatory for engine acceptance.

Until those measurements exist, a fast-callback registration is a hypothesis
for saved-method dispatch only. Neither diagnostic ratios against older
XLang3 nor source inspection proves a CPython speed win.
