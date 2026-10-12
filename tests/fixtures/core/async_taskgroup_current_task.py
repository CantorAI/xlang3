import asyncio


async def nested_task_group(parent):
    child = asyncio.current_task()
    assert child is not None and child is not parent
    assert asyncio.tasks.current_task(asyncio.get_running_loop()) is child
    assert child in asyncio.all_tasks()
    async with asyncio.TaskGroup() as nested:
        nested.create_task(asyncio.sleep(0))
    assert asyncio.current_task() is child
    assert asyncio.tasks.current_task(asyncio.get_running_loop()) is child


async def exercise(factory):
    loop = asyncio.get_running_loop()
    parent = asyncio.current_task()
    assert parent is not None
    assert asyncio.current_task(loop) is parent
    assert asyncio.tasks.current_task(loop) is parent
    assert parent in asyncio.all_tasks(loop)
    # CPython 3.14 tracks native Tasks in its intrusive task list; the Python
    # WeakSet is reserved for third-party Task implementations.
    assert parent not in asyncio.tasks._scheduled_tasks
    loop.set_task_factory(factory)
    try:
        async with asyncio.TaskGroup() as group:
            group.create_task(nested_task_group(parent))
        assert asyncio.current_task() is parent
        assert asyncio.tasks.current_task(loop) is parent
        gate = asyncio.Event()

        async def wait_for_gate():
            await gate.wait()

        pending = asyncio.create_task(wait_for_gate())
        assert pending in asyncio.all_tasks(loop)
        gate.set()
        await pending
        assert pending not in asyncio.all_tasks(loop)
    finally:
        loop.set_task_factory(None)


async def main():
    await exercise(None)
    if hasattr(asyncio, "eager_task_factory"):
        await exercise(asyncio.eager_task_factory)
    print("nested TaskGroup current task: ok")


asyncio.run(main())
