import asyncio
from asyncio import Task


original_init = Task.__init__
observed = []


def patched_init(self, *args, **kwargs):
    observed.append(self)
    return original_init(self, *args, **kwargs)


async def child():
    return 17


Task.__init__ = patched_init
try:
    async def main():
        return await asyncio.create_task(child())

    result = asyncio.run(main())
finally:
    Task.__init__ = original_init

print("task-init-override-observed", bool(observed), "result", result)
