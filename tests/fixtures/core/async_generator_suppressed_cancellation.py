import asyncio


ready = asyncio.Event()


async def wait_for_cancellation():
    ready.set()
    try:
        await asyncio.Event().wait()
    except asyncio.CancelledError:
        print("generator-cancelled")
        await asyncio.sleep(0)
        print("cleanup-resumed")


async def source():
    yield 1
    await wait_for_cancellation()


async def consume():
    iterator = source()
    print("first", await anext(iterator))
    try:
        await anext(iterator)
    except StopAsyncIteration:
        print("exhausted")
    return 9


async def cancel_and_return():
    task = asyncio.current_task()
    task.cancel("inside timeout")
    try:
        await asyncio.sleep(0)
    except asyncio.CancelledError:
        task.uncancel()
        return 17


async def with_timeout():
    return await asyncio.wait_for(cancel_and_return(), 2)


async def main():
    task = asyncio.create_task(consume())
    await ready.wait()
    assert task.cancel("probe")
    print("result", await task)
    print("timeout-result", await asyncio.create_task(with_timeout()))


asyncio.run(main())
