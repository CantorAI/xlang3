import sys

from sys import monitoring

lines = []


def line_callback(code, line):
    if code.co_name == "target":
        lines.append(line)


def target():
    monitoring.set_events(3, monitoring.events.LINE)
    first = 1
    second = 2
    return first + second


monitoring.use_tool_id(3, "midframe-probe")
monitoring.register_callback(3, monitoring.events.LINE, line_callback)
print("result", target())
print("lines", lines)
monitoring.set_events(3, 0)
monitoring.free_tool_id(3)
