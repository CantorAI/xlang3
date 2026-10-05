import asyncio
import _asyncio


native_future_type = _asyncio.Future
loop = asyncio.new_event_loop()


class ReplacementFuture:
    pass


_asyncio.Future = ReplacementFuture
try:
    future = native_future_type(loop=loop)
    print("saved-native-type", type(future) is native_future_type)
finally:
    _asyncio.Future = native_future_type
    loop.close()
