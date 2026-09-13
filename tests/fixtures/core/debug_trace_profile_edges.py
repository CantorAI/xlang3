# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

import asyncio
import sys
import threading


watched = {'plain_target', 'handled_target', 'gen_target', 'coro_target', 'opcode_target', 'thread_target'}
events = {name: [] for name in watched}

def trace(frame, event, arg):
    name = frame.f_code.co_name
    if name not in watched:
        return trace
    if event == 'call' and name == 'opcode_target':
        frame.f_trace_lines = False
        frame.f_trace_opcodes = True
    if event == 'exception':
        detail = arg[0].__name__
    elif event == 'return':
        detail = repr(arg)
    else:
        detail = ''
    events[name].append(event + (':' + detail if detail else ''))
    return trace

def plain_target(value):
    step = value + 1
    return step * 2

def handled_target():
    try:
        raise ValueError('trace')
    except ValueError:
        return 'handled'

def gen_target():
    yield 1
    return 2

async def coro_target():
    await asyncio.sleep(0)
    return 3

def opcode_target():
    x = 1
    return x + 1

def thread_target():
    return 4

sys.settrace(trace)
print('plain', plain_target(2))
print('handled', handled_target())
gen = gen_target()
print('generator', next(gen))
try:
    next(gen)
except StopIteration as exc:
    print('generator-stop', exc.value)
print('coroutine', asyncio.run(coro_target()))
print('opcode', opcode_target())
sys.settrace(None)

threading.settrace(trace)
thread = threading.Thread(target=thread_target)
thread.start(); thread.join()
threading.settrace(None)

for name in sorted(events):
    sequence = events[name]
    print(name, sequence if name != 'opcode_target' else (sequence[0], sequence[-1], sequence.count('opcode'), 'line' in sequence))


watched = {'profile_target', 'profile_gen', 'profile_coro', 'profile_thread'}
events = {name: [] for name in watched}

def profile(frame, event, arg):
    name = frame.f_code.co_name
    if name not in watched:
        return
    if event.startswith('c_'):
        arg_name = getattr(arg, '__name__', type(arg).__name__)
        if arg_name != 'len':
            return
        events[name].append(event + ':' + arg_name)
    else:
        events[name].append(event + (':' + repr(arg) if event == 'return' else ''))

def profile_target():
    size = len([1, 2])
    try:
        len(1)
    except TypeError:
        pass
    return size

def profile_gen():
    yield 5
    return 6

async def profile_coro():
    await asyncio.sleep(0)
    return 7

def profile_thread():
    return len([1, 2, 3])

sys.setprofile(profile)
print('target', profile_target())
g = profile_gen(); print('gen-yield', next(g))
try:
    next(g)
except StopIteration as exc:
    print('gen-return', exc.value)
print('coro', asyncio.run(profile_coro()))
sys.setprofile(None)
threading.setprofile(profile)
t = threading.Thread(target=profile_thread); t.start(); t.join()
threading.setprofile(None)
for name in sorted(events): print(name, events[name])


monitor = sys.monitoring
try:
    monitor.free_tool_id(monitor.DEBUGGER_ID)
except ValueError:
    pass
monitor.use_tool_id(monitor.DEBUGGER_ID, "callback lifetime")
monitor_entered = threading.Event()
monitor_release = threading.Event()
monitor_calls = []
monitor_replaced = []


def monitored_target():
    return 8


def replacement_callback(code, offset):
    return monitor.DISABLE


def blocking_callback(code, offset):
    if code is monitored_target.__code__:
        monitor_calls.append("target")
        monitor_entered.set()
        monitor_release.wait(2)
    return monitor.DISABLE


def replace_monitor_callback():
    monitor_entered.wait(2)
    previous = monitor.register_callback(
        monitor.DEBUGGER_ID, monitor.events.PY_START, replacement_callback
    )
    monitor_replaced.append(previous is blocking_callback)
    monitor_release.set()


monitor_thread = threading.Thread(target=replace_monitor_callback)
monitor_thread.start()
monitor.register_callback(monitor.DEBUGGER_ID, monitor.events.PY_START, blocking_callback)
monitor.set_events(monitor.DEBUGGER_ID, monitor.events.PY_START)
print("monitor-race-value", monitored_target())
monitor.set_events(monitor.DEBUGGER_ID, 0)
monitor_thread.join(2)
monitor.free_tool_id(monitor.DEBUGGER_ID)
print("monitor-race", monitor_calls, monitor_replaced, monitor_thread.is_alive())
