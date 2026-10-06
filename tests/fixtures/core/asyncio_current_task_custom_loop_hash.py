import asyncio


class HashedLoop(asyncio.SelectorEventLoop):
    def __init__(self):
        super().__init__()
        self.hash_calls = 0

    def __hash__(self):
        self.hash_calls += 1
        return 17


async def main(loop):
    current = asyncio.current_task()
    assert current is asyncio.tasks.current_task(loop)
    assert loop.hash_calls == 0
    assert asyncio.tasks._current_tasks == {}


loop = HashedLoop()
asyncio.set_event_loop(loop)
try:
    loop.run_until_complete(main(loop))
finally:
    asyncio.set_event_loop(None)
    loop.close()

print("custom loop hash current task: ok")
