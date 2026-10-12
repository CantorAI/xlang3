import asyncio
import sys


class MethodLoop(asyncio.SelectorEventLoop):
    def __init__(self):
        super().__init__()
        self.call_soon_count = 0

    def call_soon(self, callback, *args, context=None):
        self.call_soon_count += 1
        return super().call_soon(callback, *args, context=context)


async def work():
    await asyncio.sleep(0)
    return 42


class_loop = MethodLoop()
class_task = asyncio.Task(work(), loop=class_loop)
print("class-method", class_loop.run_until_complete(class_task), class_loop.call_soon_count > 0)
class_loop.close()

instance_loop = asyncio.new_event_loop()
original_call_soon = instance_loop.call_soon
instance_calls = []


def instance_call_soon(callback, *args, context=None):
    instance_calls.append(callback)
    return original_call_soon(callback, *args, context=context)


instance_loop.call_soon = instance_call_soon
instance_task = asyncio.Task(work(), loop=instance_loop)
print("instance-shadow", instance_loop.run_until_complete(instance_task), len(instance_calls) > 0)
instance_loop.close()

dynamic_loop = asyncio.new_event_loop()
original_class_call_soon = type(dynamic_loop).call_soon
dynamic_calls = []


def replacement_call_soon(self, callback, *args, context=None):
    dynamic_calls.append(callback)
    return original_class_call_soon(self, callback, *args, context=context)


type(dynamic_loop).call_soon = replacement_call_soon
try:
    dynamic_task = asyncio.Task(work(), loop=dynamic_loop)
    print("class-replaced", dynamic_loop.run_until_complete(dynamic_task), len(dynamic_calls) > 0)
finally:
    type(dynamic_loop).call_soon = original_class_call_soon
    dynamic_loop.close()


class FutureSubclass(asyncio.Future):
    pass


async def await_future(future):
    return await future


future_loop = asyncio.new_event_loop()
future = FutureSubclass(loop=future_loop)
future_task = asyncio.Task(await_future(future), loop=future_loop)
future_loop.call_soon(future.set_result, 7)
print("future-subclass", future_loop.run_until_complete(future_task))
future_loop.close()


class SimpleAccessorLoop(asyncio.SelectorEventLoop):
    def get_debug(self):
        return self._debug

    def is_running(self):
        return self._thread_id is not None


accessor_loop = SimpleAccessorLoop()
accessor_loop.set_debug(True)
accessor_future = asyncio.Future(loop=accessor_loop)
print("simple-accessor-debug", accessor_future._source_traceback is not None)
accessor_loop.close()

dict_loop = SimpleAccessorLoop()
dict_loop.__dict__["_debug"] = True
dict_future = asyncio.Future(loop=dict_loop)
print("simple-accessor-dict-write", dict_future._source_traceback is not None)
dict_loop.close()


profile_loop = asyncio.new_event_loop()
profiled_debug = []


def profile_get_debug(frame, event, arg):
    if event == "call" and frame.f_code.co_name == "get_debug":
        profiled_debug.append(frame)


sys.setprofile(profile_get_debug)
profiled_future = asyncio.Future(loop=profile_loop)
sys.setprofile(None)
print("profile-sees-get-debug", bool(profiled_debug))
profile_loop.close()


async def eager_accessor_task():
    async def child():
        return 91

    task = asyncio.Task(
        child(), loop=asyncio.get_running_loop(), eager_start=True
    )
    return await task


run_loop = SimpleAccessorLoop()
print("simple-accessor-running", run_loop.run_until_complete(eager_accessor_task()))
run_loop.close()
