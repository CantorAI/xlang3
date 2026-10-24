# asyncio current-task accelerator compatibility

The Release binary used by the full pyperformance run could not run Python
3.13 `asyncio.TaskGroup`: `TaskGroup.__aenter__()` raised
`cannot determine the parent task`. A focused probe showed that, inside
`asyncio.run()`, `asyncio.current_task()` returned `None` and
`asyncio.all_tasks()` was empty even though the native Task was running.

The cause was an incomplete `_asyncio` import surface. Python 3.13's
`asyncio.tasks` imports `_current_tasks`, `_scheduled_tasks`, `_eager_tasks`,
and the native task functions together. XLang3 exported `_current_tasks` and
the functions but omitted the two task sets, so that import fell back to the
pure-Python `current_task()` and `all_tasks()` implementations. Native Task
steps updated XLang3's thread-local task state and intrusive registry, not the
Python task map and sets those fallbacks read.

The fix lazily initializes the missing exports when `_asyncio` is first
imported, after standard-library paths are available. It uses `weakref.WeakSet`
for task tracking so the public Python `all_tasks()` view does not keep
otherwise-unreferenced Tasks alive. Native Tasks enter that set once at
creation; task stepping continues to use the thread-local current-task fast
path. The active-task map is mirrored with direct mapping operations at task
enter/leave/swap for compatibility with the Python-visible `_current_tasks`
mapping. Runtimes that never import `_asyncio` avoid the WeakSet initialization
cost.

The reusable diagnostic is
[`async_taskgroup_store_probe.py`](../../benchmarks/diagnostics/async_taskgroup_store_probe.py).
With the old Release executable it observed no current task, an empty
`all_tasks()`, and missing `_asyncio` task-set exports. The updated Debug
executable reports the native `current_task` binding, all required accelerator
exports, the running Task in `all_tasks()`, and successful stdlib TaskGroup
entry. The core fixture also checks normal and eager nested TaskGroups and
pending/completed Task visibility:
[`async_taskgroup_current_task.py`](../../tests/fixtures/core/async_taskgroup_current_task.py).
It passes under both XLang3 Debug and CPython 3.13. Release rebuild and
pyperformance measurement are still required before claiming a performance
gain.
