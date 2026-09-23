import asyncio


ready = asyncio.Event()


async def source():
    yield 1
    ready.set()
    try:
        await asyncio.Event().wait()
    except asyncio.CancelledError:
        print("generator-cancelled")


async def consume():
    iterator = source()
    print("first", await anext(iterator))
    try:
        await anext(iterator)
    except StopAsyncIteration:
        print("exhausted")
    return 9


async def main():
    task = asyncio.create_task(consume())
    await ready.wait()
    assert task.cancel("probe")
    print("result", await task)


asyncio.run(main())
