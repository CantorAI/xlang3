# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

import sys
import threading
import time
import _thread


print("threading-source", threading.__file__.replace("\\", "/").endswith("/Lib/threading.py"))
print("main", threading.current_thread() is threading.main_thread(), threading.active_count())

errors = []
for label, action in [
    ("join-before", lambda: threading.Thread().join()),
    ("join-current", lambda: threading.current_thread().join()),
]:
    try:
        action()
    except Exception as exc:
        errors.append((label, type(exc).__name__, str(exc)))
print("lifecycle-errors", errors)

lock = threading.Lock()
print("lock", lock.acquire(), lock.locked(), lock.acquire(False))
lock.release()
rlock = threading.RLock()
print("rlock", rlock.acquire(), rlock.acquire(False))
rlock.release()
rlock.release()
event = threading.Event()
print("event", event.is_set(), event.wait(0))
event.set()
print("event-set", event.is_set(), event.wait(0))
semaphore = threading.BoundedSemaphore(1)
print("semaphore", semaphore.acquire(False), semaphore.acquire(False))
semaphore.release()
try:
    semaphore.release()
except Exception as exc:
    print("semaphore-bound", type(exc).__name__, str(exc))

ready = threading.Event()
release = threading.Event()
seen = []
local = threading.local()
local.value = "main"


def worker():
    seen.append((threading.current_thread().name, hasattr(local, "value")))
    local.value = "worker"
    ready.set()
    release.wait()


thread = threading.Thread(target=worker, name="worker", daemon=False)
print("before", thread.ident, thread.native_id, thread.is_alive())
thread.start()
ready.wait(2)
print(
    "running",
    thread.ident is not None,
    thread.native_id is not None,
    thread.is_alive(),
    threading.active_count() >= 2,
)
try:
    thread.daemon = True
except Exception as exc:
    print("daemon-running", type(exc).__name__, str(exc))
release.set()
thread.join(2)
print("after", thread.is_alive(), thread.ident is not None, local.value, seen)
try:
    thread.start()
except Exception as exc:
    print("start-twice", type(exc).__name__, str(exc))

condition = threading.Condition()
condition_ready = threading.Event()
condition_result = []


def waiter():
    with condition:
        condition_ready.set()
        condition_result.append(condition.wait_for(lambda: "notify" in condition_result, 2))


wait_thread = threading.Thread(target=waiter)
wait_thread.start()
condition_ready.wait(2)
with condition:
    condition_result.append("notify")
    condition.notify()
wait_thread.join(2)
print("condition", condition_result, wait_thread.is_alive())

barrier = threading.Barrier(2)
barrier_results = []


def barrier_worker():
    barrier_results.append(barrier.wait(2))


barrier_thread = threading.Thread(target=barrier_worker)
barrier_thread.start()
barrier_results.append(barrier.wait(2))
barrier_thread.join(2)
print("barrier", sorted(barrier_results), barrier.broken)

trace_seen = []
profile_seen = []


def tracer(frame, event, arg):
    if frame.f_code.co_name == "hook_worker" and event == "call":
        trace_seen.append(event)
    return tracer


def profiler(frame, event, arg):
    if frame.f_code.co_name == "hook_worker" and event == "call":
        profile_seen.append(event)


def hook_worker():
    return 1


threading.settrace(tracer)
threading.setprofile(profiler)
hook_thread = threading.Thread(target=hook_worker)
hook_thread.start()
hook_thread.join()
threading.settrace(None)
threading.setprofile(None)
print("hooks", trace_seen, profile_seen)

main_ident = threading.main_thread().ident
frame_probe_ready = threading.Event()
frame_probe_result = []


def frame_probe_worker():
    frames = sys._current_frames()
    frame_probe_result.append(
        (main_ident in frames, _thread.get_ident() in frames, all(frame is not None for frame in frames.values()))
    )
    frame_probe_ready.set()


frame_probe_thread = threading.Thread(target=frame_probe_worker)
frame_probe_thread.start()
frame_probe_ready.wait(2)
frame_probe_thread.join(2)
print("current-frames", frame_probe_result)

callable_target_seen = []


class CallableThreadTarget:
    def __call__(self):
        callable_target_seen.append("called")


callable_target_handle = _thread._make_thread_handle()
_thread.start_joinable_thread(CallableThreadTarget(), handle=callable_target_handle)
callable_target_handle.join()
print("callable-thread-target", callable_target_seen)


def shutdown_worker():
    time.sleep(0.02)
    print("shutdown-worker")


threading.Thread(target=shutdown_worker, daemon=False).start()
print("shutdown-main")
