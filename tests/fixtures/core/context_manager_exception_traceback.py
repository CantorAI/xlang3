import types


class Manager:
    def __exit__(self, exc_type, exc, traceback):
        print(exc_type.__name__)
        print(exc is caught)
        print(isinstance(traceback, types.TracebackType))
        print(traceback is exc.__traceback__)
        print(traceback.tb_frame.f_code.co_name)
        return True

    def __enter__(self):
        return self


caught = None
with Manager():
    caught = ValueError("failure")
    raise caught


class AsyncManager:
    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc, traceback):
        print(exc_type.__name__)
        print(exc is async_caught)
        print(isinstance(traceback, types.TracebackType))
        print(traceback is exc.__traceback__)
        print(traceback.tb_frame.f_code.co_name)
        return True


async def async_probe():
    global async_caught
    async with AsyncManager():
        async_caught = RuntimeError("async failure")
        raise async_caught


async_caught = None
import asyncio
asyncio.run(async_probe())
