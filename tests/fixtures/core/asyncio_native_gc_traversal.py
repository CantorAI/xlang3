import asyncio
import gc
import weakref


loop = asyncio.new_event_loop()
future = asyncio.Future(loop=loop)
future.set_result(future)
future_ref = weakref.ref(future)
del future
gc.collect()
print(future_ref() is None)
loop.close()
