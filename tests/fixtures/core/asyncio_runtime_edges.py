# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

import asyncio
import inspect


async def coroutine_metadata():
    return 7


class OneTurnAwaitable:
    def __await__(self):
        yield None
        return 23


async def main():
    print("asyncio-source", asyncio.__file__.replace("\\", "/").endswith("/Lib/asyncio/__init__.py"))
    print("loop", type(asyncio.get_running_loop()).__name__)
    print("custom-await", await OneTurnAwaitable())

    events = []

    async def yielding(name):
        events.append(name + "-start")
        await asyncio.sleep(0)
        events.append(name + "-end")
        return name

    results = await asyncio.gather(yielding("a"), yielding("b"))
    print("yield", events, results)

    started = asyncio.Event()
    blocker = asyncio.Event()

    async def cancellable():
        started.set()
        await blocker.wait()

    task = asyncio.create_task(cancellable(), name="cancel-me")
    await started.wait()
    print("cancel-request", task.cancel("reason"), task.cancelling())
    try:
        await task
    except asyncio.CancelledError as exc:
        print("cancelled", task.done(), task.cancelled(), exc.args)

    cleanup = []
    cleanup_gate = asyncio.Event()

    async def inner_wait():
        try:
            await cleanup_gate.wait()
        finally:
            cleanup.append("inner")

    async def outer_wait():
        try:
            await inner_wait()
        finally:
            cleanup.append("outer")

    cleanup_task = asyncio.create_task(outer_wait())
    await asyncio.sleep(0)
    await asyncio.sleep(0)
    cleanup_coro = cleanup_task.get_coro()
    print("suspended", inspect.getcoroutinestate(cleanup_coro), cleanup_coro.cr_await is not None)
    cleanup_task.cancel()
    try:
        await cleanup_task
    except asyncio.CancelledError:
        pass
    print("cancel-cleanup", cleanup, len(cleanup_gate._waiters))

    coro = coroutine_metadata()
    print(
        "coro-created",
        inspect.getcoroutinestate(coro),
        coro.cr_running,
        coro.cr_await is None,
        coro.cr_frame is not None,
        coro.cr_code.co_name,
    )
    print("coro-result", await coro)
    print("coro-closed", inspect.getcoroutinestate(coro), coro.cr_frame is None)

    async def serve(reader, writer):
        data = await reader.readexactly(4)
        writer.write(data.upper())
        await writer.drain()
        writer.close()
        await writer.wait_closed()

    server = await asyncio.start_server(serve, "127.0.0.1", 0)
    port = server.sockets[0].getsockname()[1]
    reader, writer = await asyncio.open_connection("127.0.0.1", port)
    writer.write(b"ping")
    await writer.drain()
    print("socket", await reader.readexactly(4))
    writer.close()
    await writer.wait_closed()
    server.close()
    await server.wait_closed()

    process = await asyncio.create_subprocess_exec(
        "cmd",
        "/c",
        "(echo async-out)&(echo async-err 1>&2)&exit /b 7",
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )
    stdout, stderr = await process.communicate()
    print("process", process.returncode, stdout, stderr)

    pipe_process = await asyncio.create_subprocess_exec(
        "cmd",
        "/c",
        "sort",
        stdin=asyncio.subprocess.PIPE,
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
    )
    stdout, stderr = await pipe_process.communicate(b"pipe-in\r\n")
    print("process-input", pipe_process.returncode, stdout, stderr)


asyncio.run(main())
