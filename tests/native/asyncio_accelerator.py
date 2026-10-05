"""Native _asyncio contracts checked against CPython 3.14.

These assertions cover native/Future subclass dispatch, callback reentrancy,
iterator lifetime, Task cancellation/context/introspection and collector edges.
They are correctness tests, not performance replicas of official benchmarks.
"""
import _asyncio
import asyncio
import contextvars
import gc
import types
import warnings
import weakref


def raises(kind, function, *args, **kwargs):
    try:
        function(*args, **kwargs)
    except kind as exception:
        return exception
    raise AssertionError(f"expected {kind.__name__}")


def iterator_contract(loop):
    future = asyncio.Future(loop=loop)
    iterator = future.__await__()
    assert iter(iterator) is iterator and next(iterator) is future
    raises(RuntimeError, next, iterator)
    future._asyncio_future_blocking = False
    assert iterator.send("ignored") is future
    future.set_result(37)
    assert raises(StopIteration, next, iterator).value == 37
    assert raises(StopIteration, next, iterator).value == 37
    reference = weakref.ref(future)
    del future
    gc.collect()
    assert reference() is not None  # CPython retains after successful next().
    iterator.close()
    gc.collect()
    assert reference() is None
    # Do not advance a closed native iterator: the reference CPython source
    # clears its Future pointer without an am_send null guard. Check release
    # through weakrefs rather than relying on undefined closed-iterator use.

    future = asyncio.Future(loop=loop)
    iterator = future.__await__()
    reference = weakref.ref(future)
    del future
    raises(TypeError, iterator.throw, 42)
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", DeprecationWarning)
        raises(TypeError, iterator.throw, ValueError("a"), "extra")
        raises(TypeError, iterator.throw, ValueError, None, 42)
    assert reference() is not None  # Invalid throw must not consume ownership.
    exception = ValueError("identity")
    assert raises(ValueError, iterator.throw, exception) is exception
    gc.collect()
    assert reference() is None
    print("future-iterator-state-and-lifetime", True)


def field_contract(loop):
    future = asyncio.Future.__new__(asyncio.Future)
    assert not future.done() and not future.cancelled()
    raises(RuntimeError, future.get_loop)
    assert future._loop is None and future._source_traceback is None
    assert future._asyncio_future_blocking is False
    for field in ("_state", "_result", "_exception", "_log_traceback", "_callbacks"):
        raises(RuntimeError, getattr, future, field)
    future.__init__(loop=loop)
    assert future._state == "PENDING" and future._callbacks is None
    future._asyncio_future_blocking = 1
    assert future._asyncio_future_blocking is True
    for field in ("_state", "_loop", "_result", "_exception", "_callbacks", "_source_traceback"):
        raises(AttributeError, setattr, future, field, None)
    raises(ValueError, setattr, future, "_log_traceback", True)
    future.set_exception(ValueError("unconsumed"))
    assert future._log_traceback is True
    future._log_traceback = False
    assert not future._log_traceback
    future.exception()
    print("future-native-fields", True)


def scheduling_contract():
    first = lambda future: None
    second = lambda future: None
    third = lambda future: None

    class InspectingLoop:
        def __init__(self, remove_first):
            self.calls = []
            self.future = None
            self.remove_first = remove_first

        def get_debug(self):
            return False

        def call_soon(self, callback, *args, context=None):
            self.calls.append(callback)
            if len(self.calls) == 1:
                # Inline callback is detached; extras remain mutable here.
                assert [cb for cb, ctx in self.future._callbacks] == [second, third]
                if self.remove_first:
                    assert self.future.remove_done_callback(second) == 1
            else:
                # Extras are now detached and scheduling their owned snapshot.
                assert self.future._callbacks is None
                assert self.future.remove_done_callback(third) == 0

    for remove_first in (True, False):
        inspector = InspectingLoop(remove_first)
        future = asyncio.Future(loop=inspector)
        inspector.future = future
        for callback in (first, second, third):
            future.add_done_callback(callback)
        future.set_result(1)
        assert inspector.calls == ([first, third] if remove_first else [first, second, third])

    class FailingLoop:
        def get_debug(self):
            return False

        def call_soon(self, *args, **kwargs):
            raise RuntimeError("call-soon-failed")

    future = asyncio.Future(loop=FailingLoop())
    future.add_done_callback(first)
    future.add_done_callback(second)
    exception = ValueError("stored")
    assert raises(RuntimeError, future.set_exception, exception).args == ("call-soon-failed",)
    assert future.done() and future._callbacks is None and not future._log_traceback
    assert future.exception() is exception
    print("callback-scheduling-reentrancy-and-failure", True)


async def main():
    loop = asyncio.get_running_loop()
    assert asyncio.Future is _asyncio.Future and asyncio.Task is _asyncio.Task
    assert asyncio.Future.__module__ == asyncio.Task.__module__ == "_asyncio"
    assert asyncio.get_event_loop() is loop and _asyncio._get_running_loop() is loop
    print("native-import-and-running-loop", True)
    iterator_contract(loop)
    field_contract(loop)
    scheduling_contract()

    value = contextvars.ContextVar("native-contract", default="default")
    observed = []

    def callback(future):
        observed.append((value.get(), future.result()))

    future = loop.create_future()
    value.set("creation")
    future.add_done_callback(callback)
    explicit = contextvars.copy_context()
    explicit.run(value.set, "explicit")
    future.add_done_callback(callback, context=explicit)
    # Explicit None is kept as None; loop.call_soon captures at completion.
    future.add_done_callback(callback, context=None)
    exported = future._callbacks
    assert len(exported) == 3 and exported[2][1] is None
    exported.clear()
    assert len(future._callbacks) == 3
    value.set("completion")
    future.set_result(7)
    assert observed == []
    await asyncio.sleep(0)
    assert observed == [("creation", 7), ("explicit", 7), ("completion", 7)]
    future.add_done_callback(callback)
    assert len(observed) == 3
    await asyncio.sleep(0)
    assert observed[-1] == ("completion", 7)
    print("callbacks-context-order-and-copy", True)

    removable = loop.create_future()
    removable.add_done_callback(callback)
    removable.add_done_callback(callback)
    assert removable.remove_done_callback(callback) == 2
    assert removable._callbacks is None
    removable.set_result(9)
    await asyncio.sleep(0)
    assert len(observed) == 4

    class CompleteInEquality:
        def __call__(self, future):
            observed.append(("reentrant", future.result()))

        def __eq__(self, other):
            if not recursive.done():
                recursive.set_result(13)
            return True

    recursive = loop.create_future()
    recursive.add_done_callback(CompleteInEquality())
    recursive.add_done_callback(callback)
    assert recursive.remove_done_callback(object()) == 1
    assert recursive._callbacks is None
    await asyncio.sleep(0)
    assert observed[-2:] == [("reentrant", 13), ("completion", 13)]
    print("callback-removal-and-reentrancy", True)

    failed = loop.create_future()
    exception = ValueError("payload")
    failed.set_exception(exception)
    assert failed.exception() is exception
    assert raises(ValueError, failed.result) is exception
    stopped = loop.create_future()
    stopped.set_exception(StopIteration("stop"))
    assert isinstance(stopped.exception(), RuntimeError)
    assert isinstance(stopped.exception().__cause__, StopIteration)
    cancelled = loop.create_future()
    assert cancelled.cancel(msg="reason") and not cancelled.cancel("again")
    assert raises(asyncio.CancelledError, cancelled.result).args == ("reason",)
    assert cancelled.done() and cancelled.cancelled()
    print("future-exception-and-cancellation", True)

    async def read_context():
        await asyncio.sleep(0)
        return value.get()

    value.set("task-creation")
    task = asyncio.create_task(read_context(), name="context-task")
    value.set("outer")
    assert task.get_name() == "context-task" and task.get_context() is not None
    assert task in asyncio.all_tasks(loop)
    assert await task == "task-creation"
    assert task not in asyncio.all_tasks(loop)
    task.set_name(42)
    assert task.get_name() == "42"
    raises(RuntimeError, task.set_result, 1)
    raises(RuntimeError, task.set_exception, ValueError())
    keyword_task = asyncio.Task(coro=read_context(), loop=loop, name="keyword-coro")
    assert await keyword_task == "outer"
    print("task-context-name-and-registry", True)

    parent = asyncio.current_task()

    async def eager():
        assert asyncio.current_task().get_name() == "eager"
        return value.get()

    task = asyncio.Task(eager(), loop=loop, name="eager", eager_start=True)
    assert task.done() and task.result() == "outer" and task.get_coro() is None
    assert asyncio.current_task() is parent

    async def eager_error():
        raise ValueError("eager-error")

    task = asyncio.Task(eager_error(), loop=loop, eager_start=True)
    assert task.done() and raises(ValueError, task.result).args == ("eager-error",)
    assert task.get_coro() is None and asyncio.current_task() is parent
    print("eager-completion-error-and-current-task", True)

    if hasattr(asyncio, "eager_task_factory"):
        previous_factory = loop.get_task_factory()

        async def nested_task_group():
            child = asyncio.current_task()
            assert child is not None and child is not parent
            assert asyncio.tasks.current_task(loop) is child
            async with asyncio.TaskGroup() as nested_group:
                nested_group.create_task(asyncio.sleep(0))
            assert asyncio.current_task() is child
            assert asyncio.tasks.current_task(loop) is child

        async def exercise_task_group():
            assert asyncio.tasks.current_task(loop) is parent
            async with asyncio.TaskGroup() as task_group:
                task_group.create_task(nested_task_group())
            assert asyncio.current_task() is parent
            assert asyncio.tasks.current_task(loop) is parent

        # pyperformance's TaskGroup cases call current_task() both in ordinary
        # child Tasks and in synchronously started eager children. Keep task
        # identity correct across both the normal enter/leave and eager
        # save/restore paths.
        try:
            loop.set_task_factory(previous_factory)
            await exercise_task_group()
            loop.set_task_factory(asyncio.eager_task_factory)
            await exercise_task_group()
        finally:
            loop.set_task_factory(previous_factory)
        assert asyncio.current_task() is parent
        print("eager-taskgroup-current-task", True)

    gate = asyncio.Event()

    async def waiting():
        await gate.wait()

    task = asyncio.create_task(waiting())
    await asyncio.sleep(0)
    assert task.cancel("first") and task.cancel("second")
    assert task.cancelling() == 2 and task.uncancel() == 1
    try:
        await task
    except asyncio.CancelledError as caught:
        assert caught.args == ("first",)
    else:
        raise AssertionError("Task cancellation must propagate")
    assert task.done() and task.cancelled() and task.cancelling() == 1
    task = asyncio.create_task(asyncio.sleep(0, result=19))
    assert task.cancel("rescinded") and task.uncancel() == 0
    assert await task == 19
    print("task-cancellation-and-uncancel", True)

    awaited = loop.create_future()

    async def await_future():
        return await awaited

    task = asyncio.create_task(await_future())
    await asyncio.sleep(0)
    assert task in awaited._asyncio_awaited_by
    awaited.set_result(29)
    assert await task == 29
    assert not awaited._asyncio_awaited_by or task not in awaited._asyncio_awaited_by
    print("awaited-by-cleanup", True)

    class FutureSubclass(asyncio.Future):
        def done(self):
            return False

        def result(self):
            return "override"

    overridden = FutureSubclass(loop=loop)
    overridden.set_result(31)
    assert await overridden == 31  # Iterator reads native state.

    dispatch = []

    class WakeupSubclass(asyncio.Future):
        def get_loop(self):
            dispatch.append("get_loop")
            return super().get_loop()

        def add_done_callback(self, callback, *, context=None):
            dispatch.append("add_done_callback")
            return super().add_done_callback(callback, context=context)

        def result(self):
            dispatch.append("result")
            raise ValueError("overridden-result")

    subclass = WakeupSubclass(loop=loop)

    async def await_subclass():
        return await subclass

    task = asyncio.create_task(await_subclass())
    await asyncio.sleep(0)
    subclass.set_result(41)
    try:
        await task
    except ValueError as caught:
        assert caught.args == ("overridden-result",)
    else:
        raise AssertionError("wakeup must dispatch the subclass result method")
    assert dispatch == ["get_loop", "add_done_callback", "result"]

    class OneTurn:
        def __await__(self):
            yield None
            return 23

    assert await OneTurn() == 23

    @types.coroutine
    def bad_yield():
        yield 42

    async def bad():
        await bad_yield()

    try:
        await asyncio.create_task(bad())
    except RuntimeError as caught:
        assert "bad yield" in str(caught)
    else:
        raise AssertionError("bad yield must raise")
    print("subclass-await-and-custom-awaitables", True)

    class ForeignFuture:
        def __init__(self):
            self.inner = loop.create_future()
            self.calls = []
            self._asyncio_future_blocking = False

        def get_loop(self):
            self.calls.append("get_loop")
            return loop

        def add_done_callback(self, callback, *, context=None):
            self.calls.append("add_done_callback")
            self.inner.add_done_callback(lambda inner: callback(self), context=context)

        def result(self):
            self.calls.append("result")
            return self.inner.result()

        def cancel(self, msg=None):
            self.calls.append("cancel")
            return self.inner.cancel(msg)

        def __await__(self):
            if not self.inner.done():
                self._asyncio_future_blocking = True
                yield self
            return self.inner.result()

    foreign = ForeignFuture()

    async def await_foreign():
        return await foreign

    task = asyncio.create_task(await_foreign())
    await asyncio.sleep(0)
    assert task._fut_waiter is foreign and not foreign._asyncio_future_blocking
    foreign.inner.set_result(43)
    assert await task == 43
    assert foreign.calls == ["get_loop", "add_done_callback", "result"]
    foreign = ForeignFuture()
    task = asyncio.create_task(await_foreign())
    await asyncio.sleep(0)
    assert task.cancel("foreign-cancel")
    try:
        await task
    except asyncio.CancelledError as caught:
        assert caught.args == ("foreign-cancel",)
    else:
        raise AssertionError("foreign Future cancellation must propagate")
    assert foreign.calls == ["get_loop", "add_done_callback", "cancel", "result"]
    print("foreign-future-dispatch-and-cancellation", True)

    other_loop = asyncio.new_event_loop()
    mismatch = other_loop.create_future()

    async def await_mismatch():
        await mismatch

    try:
        await asyncio.create_task(await_mismatch())
    except RuntimeError as caught:
        assert "different loop" in str(caught)
    else:
        raise AssertionError("cross-loop awaiting must fail")
    finally:
        mismatch.cancel()
        other_loop.close()

    async def await_self():
        await asyncio.current_task()

    try:
        await asyncio.create_task(await_self())
    except RuntimeError as caught:
        assert "itself" in str(caught)
    else:
        raise AssertionError("self-await must fail")
    assert asyncio.current_task() is parent
    print("self-await-and-loop-mismatch", True)

    def future_cycle():
        cyclic = loop.create_future()
        reference = weakref.ref(cyclic)
        cyclic.set_result(cyclic)
        return reference

    reference = future_cycle()
    gc.collect()
    assert reference() is None
    print("future-cycle-collected", True)

    saved = []
    previous_handler = loop.get_exception_handler()
    loop.set_exception_handler(lambda loop, context: saved.append(context["message"]))

    def task_cycle():
        future = loop.create_future()

        async def suspended():
            await future

        task = asyncio.Task(suspended(), loop=loop, eager_start=True)
        task._log_destroy_pending = False
        return weakref.ref(task), weakref.ref(future)

    task_reference, future_reference = task_cycle()
    gc.collect()
    assert task_reference() is None and future_reference() is None
    assert saved == []
    loop.set_exception_handler(previous_handler)
    print("task-waiter-cycle-collected", True)

    _asyncio._leave_task(loop, parent)
    try:
        _asyncio._enter_task(loop, parent)
        raises(RuntimeError, _asyncio._enter_task, loop, parent)
        raises(RuntimeError, _asyncio._leave_task, loop, object())
        assert _asyncio._swap_current_task(loop, None) is parent
        assert asyncio.current_task() is None
        assert _asyncio._swap_current_task(loop, parent) is None
    finally:
        if asyncio.current_task() is not parent:
            _asyncio._swap_current_task(loop, parent)
    print("task-entry-and-swap-guards", True)


if __name__ == "__main__":
    asyncio.run(main())
