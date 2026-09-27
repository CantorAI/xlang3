import asyncio


async def raises_after_suspend():
    await asyncio.sleep(0)
    raise StopAsyncIteration


async def values():
    try:
        await raises_after_suspend()
    except StopAsyncIteration:
        yield (5,)


async def main():
    print([item async for item in values()])


asyncio.run(main())
