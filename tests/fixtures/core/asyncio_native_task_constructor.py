import asyncio
import _asyncio


async def value():
    return 41


async def check_construction():
    loop = asyncio.get_running_loop()
    task = _asyncio.Task(
        value(), loop=loop, name="native-task", eager_start=True
    )
    print("native", type(task) is _asyncio.Task, task.done(), task.result(),
          task.get_name())

    factory = asyncio.create_eager_task_factory(_asyncio.Task)
    factory_task = factory(loop, value(), name="factory", context=None)
    print("factory", type(factory_task) is _asyncio.Task,
          factory_task.done(), factory_task.result(), factory_task.get_name())

    class DerivedTask(_asyncio.Task):
        def __init__(self, coro, *, loop=None, name=None, context=None,
                     eager_start=False):
            self.initialized_by_subclass = True
            super().__init__(coro, loop=loop, name=name, context=context,
                             eager_start=eager_start)

    derived = DerivedTask(value(), loop=loop, eager_start=True)
    print("subclass", derived.initialized_by_subclass, derived.done(),
          derived.result())

    original_init = _asyncio.Task.__init__
    patched_calls = []

    def patched_init(self, coro, *, loop=None, name=None, context=None,
                     eager_start=False):
        patched_calls.append(name)
        return original_init(self, coro, loop=loop, name=name, context=context,
                             eager_start=eager_start)

    _asyncio.Task.__init__ = patched_init
    patched = _asyncio.Task(value(), loop=loop, name="patched",
                            eager_start=True)
    _asyncio.Task.__init__ = original_init
    print("patched", patched_calls, patched.done(), patched.result(),
          patched.get_name())


asyncio.run(check_construction())
