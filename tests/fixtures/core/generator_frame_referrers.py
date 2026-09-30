import asyncio
import gc
import sys


def normal_frame():
    target = []
    frame = sys._getframe()
    print("normal owner:", frame.f_generator)
    print("normal referrers:", sorted(type(item).__name__ for item in gc.get_referrers(target)))


def generator_body():
    target = []
    frame = sys._getframe()
    print("generator owner:", frame.f_generator is generator)
    print("generator referrers:", sorted(type(item).__name__ for item in gc.get_referrers(target)))
    yield None


async def coroutine_body():
    target = []
    frame = sys._getframe()
    owner = asyncio.current_task().get_coro()
    print("coroutine owner:", frame.f_generator is owner)
    print("coroutine referrers:", sorted(type(item).__name__ for item in gc.get_referrers(target)))


normal_frame()
generator = generator_body()
next(generator)
asyncio.run(coroutine_body())
