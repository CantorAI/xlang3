"""Compare async method-call assignment with asyncio.TaskGroup.__aenter__."""

import asyncio
import _asyncio
from asyncio import events, tasks


class Box:
    pass


async def assign_current_task(box):
    box.parent = asyncio.tasks.current_task(asyncio.get_running_loop())
    return box.parent


class GroupShape:
    def __init__(self):
        self._entered = False
        self._exiting = False
        self._aborting = False
        self._loop = None
        self._parent_task = None
        self._parent_cancel_requested = False
        self._tasks = set()
        self._errors = []
        self._base_error = None
        self._on_completed_fut = None


async def taskgroup_shape(group, expected_parent):
    if group._entered:
        raise RuntimeError("already entered")
    if group._loop is None:
        group._loop = events.get_running_loop()
    group._parent_task = tasks.current_task(group._loop)
    parent_task = group._parent_task
    print("TaskGroup-shaped current-task result is parent:",
          parent_task is expected_parent)
    if parent_task is None:
        raise RuntimeError("cannot determine parent task")
    group._entered = True
    return group._parent_task


async def main():
    parent = asyncio.current_task()
    print("main current task exists:", parent is not None)
    print("asyncio Task is Python fallback Task:",
          asyncio.Task is asyncio.tasks._PyTask)
    print("asyncio tasks Task is Python fallback Task:",
          asyncio.tasks.Task is asyncio.tasks._PyTask)
    print("asyncio current_task is native _asyncio.current_task:",
          asyncio.current_task is _asyncio.current_task)
    print("_asyncio accelerator import attributes:",
          all(hasattr(_asyncio, name) for name in (
              "_scheduled_tasks", "_eager_tasks", "_current_tasks")))
    active_tasks = asyncio.all_tasks()
    print("active task count:", len(active_tasks))
    if active_tasks:
        print("active task class is native Task:",
              type(next(iter(active_tasks))) is asyncio.Task)
    print("all_tasks contains active task:", any(task is parent for task in active_tasks))
    print("tasks current-task map contains active task:",
          asyncio.tasks._current_tasks.get(asyncio.get_running_loop()) is parent)
    box = Box()
    returned = await assign_current_task(box)
    print("user coroutine call returned parent:", returned is parent)
    print("user coroutine attr stored parent:", box.parent is parent)
    print("current task after nested coroutine:", asyncio.current_task() is parent)

    shape = GroupShape()
    returned = await taskgroup_shape(shape, parent)
    print("TaskGroup-shaped coroutine returned parent:", returned is parent)
    print("TaskGroup-shaped attr stored parent:", shape._parent_task is parent)

    group = asyncio.TaskGroup()
    try:
        await group.__aenter__()
        print("stdlib TaskGroup stored parent:", group._parent_task is parent)
        await group.__aexit__(None, None, None)
    except RuntimeError as error:
        print("stdlib TaskGroup error:", str(error))


asyncio.run(main())
