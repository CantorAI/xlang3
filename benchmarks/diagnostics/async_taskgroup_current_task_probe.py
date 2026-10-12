"""Expose current-task lookup differences seen by asyncio.TaskGroup."""

import asyncio


async def main():
    loop = asyncio.get_running_loop()
    parent = asyncio.current_task()
    original_current_task = asyncio.tasks.current_task
    taskgroup_tasks = asyncio.TaskGroup.__aenter__.__globals__["tasks"]
    print("TaskGroup tasks module is asyncio.tasks:", taskgroup_tasks is asyncio.tasks)
    print("TaskGroup current_task is asyncio.tasks.current_task:",
          taskgroup_tasks.current_task is original_current_task)

    def inspect_current_task(requested_loop=None):
        running_loop = asyncio.get_running_loop()
        implicit = original_current_task()
        explicit = (original_current_task(requested_loop)
                    if requested_loop is not None else implicit)
        print(
            "current_task lookup:",
            "requested_loop_is_running=", requested_loop is running_loop,
            "implicit_is_parent=", implicit is parent,
            "explicit_is_parent=", explicit is parent,
        )
        return explicit

    asyncio.tasks.current_task = inspect_current_task
    group = asyncio.TaskGroup()
    try:
        await group.__aenter__()
        print("TaskGroup parent is parent:", group._parent_task is parent)
        await group.__aexit__(None, None, None)
    except RuntimeError as error:
        print("TaskGroup error:", str(error))
        print("stored TaskGroup parent is None:", group._parent_task is None)
    asyncio.tasks.current_task = lambda requested_loop=None: parent
    constant_group = asyncio.TaskGroup()
    try:
        await constant_group.__aenter__()
        print("constant lookup stored parent:", constant_group._parent_task is parent)
        await constant_group.__aexit__(None, None, None)
    except RuntimeError as error:
        print("constant lookup TaskGroup error:", str(error))
    finally:
        asyncio.tasks.current_task = original_current_task
    print("loop identity before group is current:", loop is asyncio.get_running_loop())


asyncio.run(main())
