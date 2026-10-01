"""Check Task/Future semantics before implementing XLang3's native _asyncio.

These are correctness probes, not timings. Public behavior is asserted; the
native-specific subclass-await observation is reported separately because
CPython's Python Future fallback deliberately calls overridden methods.
"""
import asyncio
import contextvars
import gc
import weakref


async def main():
    loop = asyncio.get_running_loop()
    print("implementations", asyncio.Future.__module__, asyncio.Task.__module__)
    value = contextvars.ContextVar("callback-value", default="default")
    observed = []
    future = loop.create_future()
    assert future.get_loop() is loop and not future.done()
    try:
        future.result()
    except asyncio.InvalidStateError:
        pass
    else:
        raise AssertionError("pending Future.result() must raise")

    def callback(fut):
        observed.append((value.get(), fut.result()))

    value.set("captured")
    future.add_done_callback(callback)
    explicit = contextvars.copy_context()
    explicit.run(value.set, "explicit")
    future.add_done_callback(callback, context=explicit)
    value.set("caller")
    future.set_result(7)
    assert observed == []
    await asyncio.sleep(0)
    assert observed == [("captured", 7), ("explicit", 7)]
    future.add_done_callback(callback)
    assert len(observed) == 2
    await asyncio.sleep(0)
    assert observed[-1] == ("caller", 7)
    print("callbacks-context-order", observed)

    removable = loop.create_future()
    removable.add_done_callback(callback)
    removable.add_done_callback(callback)
    assert removable.remove_done_callback(callback) == 2
    removable.set_result(9)
    await asyncio.sleep(0)
    assert len(observed) == 3
    print("callback-removal", True)

    failed = loop.create_future()
    exception = ValueError("payload")
    failed.set_exception(exception)
    assert failed.exception() is exception
    try:
        await failed
    except ValueError as caught:
        assert caught is exception
    else:
        raise AssertionError("await must propagate the original exception")
    stopped = loop.create_future()
    stopped.set_exception(StopIteration("stop"))
    assert isinstance(stopped.exception(), RuntimeError)
    assert isinstance(stopped.exception().__cause__, StopIteration)
    print("exception-propagation", True)

    cancelled = loop.create_future()
    assert cancelled.cancel("reason") is True
    assert cancelled.cancel("again") is False
    try:
        cancelled.result()
    except asyncio.CancelledError as caught:
        assert caught.args == ("reason",)
    else:
        raise AssertionError("cancelled result must raise")
    assert cancelled.cancelled() and cancelled.done()
    print("future-cancellation", True)

    gate = asyncio.Event()

    async def waiting():
        await gate.wait()

    task = asyncio.create_task(waiting(), name="contract-cancel")
    await asyncio.sleep(0)
    assert task.cancel("first") and task.cancel("second")
    assert task.cancelling() == 2 and task.uncancel() == 1
    try:
        await task
    except asyncio.CancelledError as caught:
        # The already-cancelled waiter supplies the first cancellation.
        assert caught.args == ("first",)
    else:
        raise AssertionError("task cancellation must propagate")
    assert task.done() and task.cancelled() and task.cancelling() == 1
    print("task-cancellation-counter", True)

    async def read_context():
        await asyncio.sleep(0)
        return value.get()

    value.set("task-creation")
    context_task = asyncio.create_task(read_context())
    value.set("outer")
    assert await context_task == "task-creation"
    print("task-context", True)

    async def eager():
        return value.get(), asyncio.current_task().get_name()

    eager_task = asyncio.Task(eager(), loop=loop, name="eager", eager_start=True)
    assert eager_task.done() and eager_task.result() == ("outer", "eager")
    assert eager_task.get_coro() is None
    print("eager-task-current-context", True)

    awaited = loop.create_future()

    async def await_future():
        return await awaited

    waiter = asyncio.create_task(await_future())
    await asyncio.sleep(0)
    assert waiter in awaited._asyncio_awaited_by
    awaited.set_result(29)
    assert await waiter == 29
    remaining = awaited._asyncio_awaited_by
    assert not remaining or waiter not in remaining
    print("awaited-by-tracking", True)

    class OverriddenFuture(asyncio.Future):
        def done(self):
            return False

    overridden = OverriddenFuture(loop=loop)
    overridden.set_result(31)
    try:
        result = await overridden
    except RuntimeError:
        native_await_ignores_override = False
    else:
        native_await_ignores_override = result == 31
    print("native-await-ignores-done-override", native_await_ignores_override)

    def future_cycle():
        cyclic = loop.create_future()
        reference = weakref.ref(cyclic)
        cyclic.set_result(cyclic)
        return reference

    reference = future_cycle()
    gc.collect()
    assert reference() is None
    print("future-result-cycle-collected", True)


if __name__ == "__main__":
    asyncio.run(main())
