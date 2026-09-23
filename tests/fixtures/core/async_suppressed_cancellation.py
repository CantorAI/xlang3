import asyncio


async def child():
    try:
        await asyncio.Event().wait()
    except asyncio.CancelledError:
        print("child-cancelled")

    return 7


async def parent():
    result = await child()
    print("parent-result", result)
    return result


async def main():
    task = asyncio.create_task(parent())
    await asyncio.sleep(0)
    assert task.cancel("probe")
    result = await task
    assert result == 7
    print("task-result", result)


asyncio.run(main())
