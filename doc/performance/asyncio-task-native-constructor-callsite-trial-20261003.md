# Native `_asyncio.Task` CallEx fast path trial (2026-10-03)

## Hypothesis

The setup-subtracted native VM sample placed about half of measured CallEx
cycles at the `asyncio.eager_task_factory` call that creates each Task. The VM
normally sends an exact `_asyncio.Task(...)` call through generic metaclass,
allocator, initializer, and class-call checks. This trial added a guarded path
for the exact native Task class, direct `object.__new__`, and the original
native Task initializer. Python `__init__` replacements, custom allocators,
subclasses, custom metaclasses, and altered layouts stayed on generic dispatch.
The Python `asyncio` factory implementation remained unchanged.

An override fixture confirmed that replacing `Task.__init__` with a Python
wrapper remains observable and still constructs a working Task. Twenty focused
fixtures covering Task/asyncio, descriptors, class behavior, Decimal, JSON, and
native imports passed.

## Measurement and decision

The official pyperformance 1.14.0 `async_tree_eager` debug mode was measured
in two alternating pairs. It returned one score per run:

| Pair | Control | Candidate | Candidate change |
|---|---:|---:|---:|
| 1 (control first) | 3.34 s | 3.30 s | 1.2% faster |
| 2 (candidate first) | 3.20 s | 3.46 s | 8.1% slower |

The candidate mean was 3.38 s versus 3.27 s for control, about 3.4% slower.
The direction is not consistent, and the score variation is much larger than
the first pair's apparent gain. The fast path was removed; it does not qualify
as a performance improvement.

Raw results and the pre-change/candidate executables are preserved in
`scratch/performance-trials/asyncio-task-constructor-callsite-20261003/`.

The accompanying exclusive Task initializer sample attributed only about
3.1 µs to Future initialization, 1.2 µs to payload/context setup, and 2.3 µs to
publish/truth checks per sampled Task. Most time was in eager start, which
synchronously executes the Python coroutine body. Together with prior rejected
Task callback dispatch work, this points toward shared VM execution costs
inside eager coroutine startup rather than another Task-constructor shortcut.
