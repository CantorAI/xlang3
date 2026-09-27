import asyncio
import gc


marker = []


def generator_result():
    return marker
    yield


generator = generator_result()
for attempt in range(2):
    try:
        next(generator)
    except StopIteration as error:
        print("generator result", attempt, error.value is marker, error.value is None)


async def coroutine_result():
    return marker


coroutine = coroutine_result()
for attempt in range(2):
    try:
        coroutine.send(None)
    except StopIteration as error:
        print("coroutine result", attempt, error.value is marker)
    except RuntimeError as error:
        print("coroutine reused", attempt, str(error))


async def rejected_future():
    future = asyncio.get_running_loop().create_future()
    error = OSError("sample")
    future.set_exception(error)
    try:
        await future
    except OSError as caught:
        return caught


async def inspect_result():
    error = await rejected_future()
    names = []
    for referrer in gc.get_referrers(error):
        name = type(referrer).__name__
        if hasattr(referrer, "cr_code"):
            name += ":" + referrer.cr_code.co_name
        elif hasattr(referrer, "f_code"):
            name += ":" + referrer.f_code.co_name
        names.append(name)
    print("coroutine result referrers", sorted(names))


asyncio.run(inspect_result())
