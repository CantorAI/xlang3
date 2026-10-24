# Plain native `__init__` unbound-call trial (2026-10-03)

## Hypothesis

The `async_tree_eager` native VM sample attributed about half of the measured
CallEx cost to the repeated Task-factory call. Class construction normally
resolved `_asyncio.Task.__init__` into a temporary bound method, then unwrapped
that method in the regular callable path. CPython's native call machinery can
pass `self` directly. The trial made the same change in the general class
construction path whenever the resolved initializer was a plain native
function, leaving Python functions and custom descriptors on the normal path.

This was a VM call optimization; it did not replace `asyncio` or any other
pure-Python module.

## Validation and result

Nineteen focused fixtures covering class construction, descriptors, Decimal,
JSON, Task scheduling, and asyncio overrides passed. The full CTest run had 48
passes and two unrelated failures: the fixture runner reached a Windows
`ctypes`/libffi requirement, and the Visual Studio debug-launch smoke test
rejected the configured launch profile.

Two order-reversed pyperformance 1.14.0 `async_tree_eager` debug pairs showed
no improvement:

| Pair | Control | Candidate | Candidate change |
|---|---:|---:|---:|
| 1 (control first) | 3.17 s | 3.21 s | 1.3% slower |
| 2 (candidate first) | 3.23 s | 3.24 s | 0.3% slower |

The combined directional result is about 0.8% slower. The optimization was
removed and the Release runtime rebuilt from the source without it. Raw
pyperf JSON and the candidate binary are preserved in
`scratch/performance-trials/asyncio-native-init-unbound-20261003/`.

The CallEx sample therefore includes costs beyond bound-method allocation.
Further work should split the native Task initialization stages or measure
the VM call binding path before trying another constructor shortcut.
