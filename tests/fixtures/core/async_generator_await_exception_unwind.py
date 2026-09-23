import asyncio
from contextlib import asynccontextmanager


events = []


class AsyncGuard:
    async def __aenter__(self):
        events.append("async-enter")
        return self

    async def __aexit__(self, exc_type, exc, traceback):
        events.append(("async-exit", exc_type.__name__ if exc_type else None))


class SyncGuard:
    def __enter__(self):
        events.append("sync-enter")
        return self

    def __exit__(self, exc_type, exc, traceback):
        events.append(("sync-exit", exc_type.__name__ if exc_type else None))


async def guarded_failure():
    async with AsyncGuard():
        with SyncGuard():
            future = asyncio.get_running_loop().create_future()
            asyncio.get_running_loop().call_soon(
                future.set_exception, ValueError("worker failed")
            )
            return await future


@asynccontextmanager
async def manager():
    yield await guarded_failure()


async def main():
    try:
        async with manager():
            pass
    except ValueError as exc:
        print(type(exc).__name__, str(exc))
        traceback = exc.__traceback__
        frames = []
        while traceback is not None:
            frames.append(traceback.tb_frame.f_code.co_name)
            traceback = traceback.tb_next
        print("guarded_failure" in frames)

    print(events)


asyncio.run(main())
