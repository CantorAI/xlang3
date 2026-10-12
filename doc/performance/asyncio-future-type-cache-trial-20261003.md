# Asyncio Future exact-type cache trial (2026-10-03)

## Result

Rejected. Caching `_asyncio.Future` and `_asyncio.Task` class identities at the
native Task yield check did not establish a repeatable pyperformance gain.
The official Python 3.14.7 `async_tree_eager` benchmark remained around 3.2–3.4
seconds, compared with 86.6 ms for CPython 3.14.7. No runtime change from this
trial remains in the source tree.

The hypothesis was that `asyncio_task.cpp::exact_future()` repeated
`Runtime::import_module("_asyncio")` and two native module attribute lookups
for each Task yield. The candidate cached class identities per running thread
and refreshed them when `_asyncio`'s module version changed. A second variant
removed the per-yield `sys.modules` lookup while retaining module-version
invalidation. The existing exact-type guard continued to route Future
subclasses through dynamic protocol handling.

## Measurements

All runs used pyperformance 1.14.0 `--fast`, CPython 3.14.7's standard library,
the same shared dependency site, and the repository's Windows compatibility
shim. Pyperf marked all distributions as unstable.

| Variant | Run 1 | Run 2 | Mean of run means |
|---|---:|---:|---:|
| Original control | 3.32 s ± 0.18 s | 3.39 s ± 0.22 s | 3.355 s |
| Cached types, module-version guard | 3.18 s ± 0.07 s | 3.27 s ± 0.19 s | 3.225 s |
| Cached module, no per-yield registry lookup | 3.27 s ± 0.25 s | 3.39 s ± 0.24 s | 3.330 s |

The first variant's apparent 3.9% average improvement was not confirmed by the
second variant or by separate one-shot debug samples, which varied from 3.16
to 3.58 seconds. Host variation is large enough that this evidence does not
justify retaining a new thread-local owning module reference and cache
invalidation path. The much larger asyncio gap remains unresolved.

## Correctness checks and raw evidence

The focused module-replacement fixture passed on XLang3 and CPython 3.14.7.
The existing `asyncio_native_call_method_dispatch.py` fixture also passed on
XLang3, including subclass Future behavior. The full fixture runner stopped at
the known `ctypes_pointer_return` failure because libffi is unavailable; no
full-suite fixture pass is claimed.

The raw benchmark JSON, including each candidate and control identity, is in
[`scratch/performance-trials/asyncio-future-type-cache-20261003`](../../scratch/performance-trials/asyncio-future-type-cache-20261003).
The tested executable path remained
`D:\CantorAI\xlang3\build-repro\Release\xlang3.exe` throughout.
