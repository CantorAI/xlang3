import asyncio


scheduled_tasks = asyncio.tasks._scheduled_tasks


async def exercise():
    loop = asyncio.get_running_loop()
    native = asyncio.create_task(asyncio.sleep(0))
    assert native not in scheduled_tasks
    await native

    # CPython 3.14 keeps Python-implemented third-party Tasks in the WeakSet.
    fallback = asyncio.tasks._PyTask(asyncio.sleep(0), loop=loop)
    assert fallback in scheduled_tasks
    await fallback


original_add = scheduled_tasks.add
observed = []


def patched_add(task):
    observed.append(task)
    return original_add(task)


scheduled_tasks.add = patched_add
asyncio.run(exercise())
scheduled_tasks.add = original_add
print("native task skips WeakSet and Python Task uses patched add:",
      len(observed) == 1 and isinstance(observed[0], asyncio.tasks._PyTask))
