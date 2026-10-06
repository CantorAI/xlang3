import asyncio
import _asyncio


native_future = _asyncio.Future
loop = asyncio.new_event_loop()


async def check_construction():
    running_loop = asyncio.get_running_loop()
    future = native_future()
    print("native", type(future) is native_future,
          future.get_loop() is running_loop, future.done())
    explicit_loop = native_future(loop=running_loop)
    print("explicit-loop", explicit_loop.get_loop() is running_loop)

    class DerivedFuture(native_future):
        def __init__(self):
            self.initialized_by_subclass = True
            super().__init__()

    derived = DerivedFuture()
    print("subclass", derived.initialized_by_subclass,
          derived.get_loop() is running_loop)

    class DerivedFutureWithLoop(native_future):
        def __init__(self, loop):
            super().__init__(loop=loop)

    derived_with_loop = DerivedFutureWithLoop(running_loop)
    print("subclass-explicit-loop",
          derived_with_loop.get_loop() is running_loop)


try:
    loop.run_until_complete(check_construction())
finally:
    loop.close()
