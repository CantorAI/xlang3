# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0.
import sys

events = []
disabled_events = []
disabled = False
watched = {'outer_off', 'outer_resume', 'disable', 'marker'}

def trace(frame, event, arg):
    global disabled
    name = frame.f_code.co_name
    if name in watched:
        if disabled:
            disabled_events.append(name + ':' + event)
        if event in ('call', 'return'):
            events.append(name + ':' + event)
    if name == 'disable' and event == 'call':
        disabled = True
        sys.settrace(None)
        return None
    return trace

def disable():
    return 1

def marker():
    return 2

def outer_off():
    disable()
    marker()
    return 7

def outer_resume():
    global disabled
    disable()
    marker()
    disabled = False
    sys.settrace(trace)
    marker()
    return 8

sys.settrace(trace)
value = outer_off()
sys.settrace(None)
print('off', value, events, disabled_events)

events.clear()
disabled_events.clear()
disabled = False
sys.settrace(trace)
value = outer_resume()
sys.settrace(None)
print('resume', value, events, disabled_events)
