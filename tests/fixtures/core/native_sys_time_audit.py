import _sysconfig
import sys


config_vars = _sysconfig.config_vars()
expected_gil = not bool(config_vars["Py_GIL_DISABLED"])
print(
    "sys-native-state",
    sys.is_remote_debug_enabled() is False,
    isinstance(sys._is_gil_enabled(), bool),
    sys._is_gil_enabled() is expected_gil,
)

startup_hook = sys.__interactivehook__
import site

print(
    "sys-startup-hooks",
    startup_hook.__module__ == "site",
    startup_hook.__name__ == "register_readline",
    startup_hook() is None,
    sys.__interactivehook__ is site.register_readline,
    hasattr(sys.__interactivehook__, "__code__"),
    callable(sys._baserepl),
)

trampoline_results = []
for backend in ("perf", "perf_jit", "invalid"):
    try:
        sys.activate_stack_trampoline(backend)
    except ValueError as exc:
        trampoline_results.append(str(exc) == "perf trampoline not available")
print(
    "sys-stack-trampoline",
    trampoline_results == [True, True, True],
    sys.is_stack_trampoline_active() is False,
    sys.deactivate_stack_trampoline() is None,
    sys.is_stack_trampoline_active() is False,
)

print(
    "sys-jit-state",
    sys._jit.is_available() is False,
    sys._jit.is_enabled() is False,
    sys._jit.is_active() is False,
)

monitoring = sys.monitoring
events = monitoring.events
tool_id = 4
seen = []


def record(name):
    def callback(*args):
        seen.append((name, len(args), getattr(args[0], "co_name", None), args[1]))

    return callback


def monitored(flag):
    size = len((1, 2))
    sys.getsizeof(size)
    if flag:
        return size + 1
    return size


monitoring.use_tool_id(tool_id, "native-sys-time-audit")
for event, name in (
    (events.PY_START, "start"),
    (events.LINE, "line"),
    (events.INSTRUCTION, "instruction"),
    (events.BRANCH_LEFT, "branch-left"),
    (events.BRANCH_RIGHT, "branch-right"),
    (events.CALL, "call"),
    (events.C_RETURN, "c-return"),
    (events.PY_RETURN, "return"),
):
    monitoring.register_callback(tool_id, event, record(name))

event_set = (
    events.PY_START
    | events.LINE
    | events.INSTRUCTION
    | events.BRANCH_LEFT
    | events.BRANCH_RIGHT
    | events.CALL
    | events.PY_RETURN
)
monitoring.set_events(tool_id, event_set)
monitoring_result = monitored(True)
monitoring.set_events(tool_id, 0)


def saw(name, argc):
    return any(item[0] == name and item[1] == argc and item[2] == "monitored" for item in seen)


def saw_shape(name, argc):
    return any(item[0] == name and item[1] == argc for item in seen)


def saw_source_line(name):
    first_body_line = monitored.__code__.co_firstlineno + 1
    return any(
        item[0] == name
        and item[1] == 2
        and item[2] == "monitored"
        and item[3] >= first_body_line
        for item in seen
    )


print(
    "sys-monitoring-dispatch",
    monitoring_result == 3,
    saw("start", 2),
    saw_source_line("line"),
    saw("instruction", 2),
    saw("branch-left", 3),
    saw_shape("call", 4),
    saw_shape("c-return", 4),
    saw("return", 3),
)

monitoring.set_local_events(tool_id, monitored.__code__, events.BRANCH)
print(
    "sys-monitoring-branch-alias",
    monitoring.get_local_events(tool_id, monitored.__code__)
    == events.BRANCH_LEFT | events.BRANCH_RIGHT,
)
monitoring.free_tool_id(tool_id)
monitoring.use_tool_id(tool_id, "native-sys-time-audit-reuse")
print(
    "sys-monitoring-cleanup",
    monitoring.get_events(tool_id) == 0,
    monitoring.get_local_events(tool_id, monitored.__code__) == 0,
    monitoring._all_events() == {},
)
monitoring.free_tool_id(tool_id)
