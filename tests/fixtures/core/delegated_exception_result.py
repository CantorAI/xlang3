import asyncio


async def recover():
    try:
        await asyncio.sleep(0)
    except ValueError:
        return 42


async def wrapper():
    return await recover()


coroutine = wrapper()
print("first", coroutine.send(None))
try:
    coroutine.throw(ValueError("worker failure"))
except StopIteration as error:
    print("result", error.value)
