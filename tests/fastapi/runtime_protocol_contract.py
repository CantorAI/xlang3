import asyncio
import operator

from anyio.functools import lru_cache
from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get("/runtime-protocol")
async def runtime_protocol():
    delegate_events = []

    class Cached:
        @classmethod
        @lru_cache
        async def value(cls, number: int) -> int:
            return number + 1

    class Delegate:
        def __init__(self):
            self.count = 0

        def __iter__(self):
            return self

        def __next__(self):
            self.count += 1
            if self.count == 1:
                return "ready"
            raise StopIteration("complete")

        def send(self, value):
            return f"sent:{value}"

        def throw(self, exception):
            delegate_events.append(type(exception).__name__)
            return "recovered"

        def close(self):
            delegate_events.append("closed")

    def outer():
        return (yield from Delegate())

    iterator = outer()
    first = next(iterator)
    second = iterator.send(5)
    try:
        next(iterator)
    except StopIteration as exc:
        returned = exc.value

    thrower = outer()
    next(thrower)
    thrown = thrower.throw(ValueError("probe"))
    try:
        next(thrower)
    except StopIteration as exc:
        throw_returned = exc.value

    closer = outer()
    next(closer)
    closer.close()

    try:
        operator.index("bad")
    except TypeError as exc:
        index_error = type(exc).__name__

    async def values():
        yield 1

    awaitable = values().asend(None)
    coroutine = asyncio.coroutines.iscoroutine(awaitable)
    awaitable.close()

    async def raises_after_suspend():
        await asyncio.sleep(0)
        raise StopAsyncIteration

    async def recovers():
        try:
            await raises_after_suspend()
        except StopAsyncIteration:
            yield "recovered"

    return {
        "cached": await Cached.value(7),
        "delegated": [first, second, returned],
        "delegate_throw_close": [thrown, throw_returned, delegate_events],
        "index_error": index_error,
        "socket_error": type(OSError(10061, "refused")).__name__,
        "asend_coroutine": coroutine,
        "recovered": [item async for item in recovers()],
    }


response = TestClient(app).get("/runtime-protocol")
print(response.status_code)
print(response.json())
