"""Launch and debug XLang3 through Visual Studio's unmodified debugpy stack."""

from __future__ import annotations

import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading
import time


ROOT = Path(__file__).resolve().parents[2]
LAUNCH_CONFIG_PATH = ROOT / "tests" / "ide" / "vs2026_launch.json"
LAUNCH_CONFIG = json.loads(LAUNCH_CONFIG_PATH.read_text(encoding="utf-8"))
IDE_PROFILE_PATH = ROOT / "launch.vs.json"
IDE_PROFILE_DOCUMENT = json.loads(IDE_PROFILE_PATH.read_text(encoding="utf-8"))
IDE_PROFILE = next(
    item for item in IDE_PROFILE_DOCUMENT["configurations"]
    if item.get("name") == "XLang3 Python 3.14 (debugpy)"
)
XLANG = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build" / "Release" / "xlang3.exe"
REFERENCE_RUN = os.environ.get("XLANG3_DEBUGPY_REFERENCE_RUN") == "1"
ADAPTER_PYTHON = XLANG if REFERENCE_RUN else Path(os.environ.get(
    "XLANG3_VS_ADAPTER_PYTHON", str(LAUNCH_CONFIG["$adapter"])
)).resolve()
CONFIG_ADAPTER_ENTRY = str(LAUNCH_CONFIG["$adapterArgs"]).strip()
if CONFIG_ADAPTER_ENTRY.startswith('"') and CONFIG_ADAPTER_ENTRY.endswith('"'):
    CONFIG_ADAPTER_ENTRY = CONFIG_ADAPTER_ENTRY[1:-1]
ADAPTER_ENTRY = Path(os.environ.get("XLANG3_VS_ADAPTER", CONFIG_ADAPTER_ENTRY)).resolve()
VS_CORE = Path(os.environ.get(
    "XLANG3_VS_CORE",
    str(LAUNCH_CONFIG["env"]["XLANG3_PYTHON_LIB"]),
)).resolve()
PROGRAM = Path(LAUNCH_CONFIG["program"]).resolve()
DEFAULT_TIMEOUT = float(os.environ.get("DEBUGPY_SMOKE_TIMEOUT", "180"))
ADAPTER_PROCESS: subprocess.Popen[bytes] | None = None
START_TIME = time.monotonic()


def progress(message: str) -> None:
    print(f"[{time.monotonic() - START_TIME:7.3f}s] {message}", flush=True)


def same_path(left: Path, right: Path) -> bool:
    return str(left.resolve()).replace("\\", "/").casefold() == str(right.resolve()).replace("\\", "/").casefold()


def dap_frame(message: dict[str, object]) -> bytes:
    body = json.dumps(message, separators=(",", ":")).encode("utf-8")
    return b"Content-Length: " + str(len(body)).encode("ascii") + b"\r\n\r\n" + body


def main() -> int:
    global ADAPTER_PROCESS
    if not (ADAPTER_ENTRY / "__main__.py").is_file():
        print("debugpy launch smoke skipped: Visual Studio debugpy is unavailable")
        return 0
    if not same_path(ADAPTER_ENTRY, VS_CORE / "debugpy" / "adapter"):
        raise RuntimeError("Visual Studio launch config adapter and Python Core paths disagree")
    if not same_path(PROGRAM, ROOT / "tests" / "ide" / "vs2026_debug_smoke.py"):
        raise RuntimeError("Visual Studio launch config does not target the repository smoke program")
    profile_program = (ROOT / str(IDE_PROFILE["project"])).resolve()
    profile_interpreter = XLANG if REFERENCE_RUN else Path(str(IDE_PROFILE["interpreter"])).resolve()
    if not REFERENCE_RUN:
        if not same_path(profile_interpreter, XLANG):
            raise RuntimeError("Visual Studio profile does not use xlang3.exe directly")
        if not same_path(ADAPTER_PYTHON, XLANG):
            raise RuntimeError("Visual Studio launch config does not run the adapter with xlang3.exe")
    if IDE_PROFILE.get("interpreterArguments") != "-S":
        raise RuntimeError("Visual Studio profile has unexpected interpreter arguments")
    if not same_path(profile_program, PROGRAM) or not same_path(Path(str(IDE_PROFILE["workingDirectory"])), ROOT):
        raise RuntimeError("Visual Studio profile and adapter launch config select different programs or working directories")
    if IDE_PROFILE.get("env") != LAUNCH_CONFIG.get("env"):
        raise RuntimeError("Visual Studio profile and adapter launch config use different environments")

    env = os.environ.copy()
    env["XLANG3_PYTHON_LIB"] = str(VS_CORE)
    env["XLANG3_DEBUGPY_ROOT"] = str(VS_CORE / "debugpy")
    env["PYTHONPATH"] = str(VS_CORE)
    env["PYTHONPYCACHEPREFIX"] = str(IDE_PROFILE["env"]["PYTHONPYCACHEPREFIX"])
    process = subprocess.Popen(
        [str(profile_interpreter), str(ADAPTER_ENTRY)], cwd=ROOT, env=env,
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
    )
    ADAPTER_PROCESS = process
    progress(f"adapter started with {profile_interpreter.name} (pid {process.pid})")
    assert process.stdin is not None and process.stdout is not None
    incoming: queue.Queue[dict[str, object] | BaseException | None] = queue.Queue()
    backlog: list[dict[str, object]] = []
    perf_lines: list[str] = []
    sequence = 0

    def read_messages() -> None:
        try:
            while True:
                headers: dict[bytes, bytes] = {}
                while True:
                    line = process.stdout.readline()
                    if not line:
                        incoming.put(None)
                        return
                    line = line.rstrip(b"\r\n")
                    if not line:
                        break
                    key, value = line.split(b":", 1)
                    headers[key.lower()] = value.strip()
                message = json.loads(process.stdout.read(int(headers[b"content-length"])))
                if message.get("type") == "event" and message.get("event") == "output":
                    output_text = str(message.get("body", {}).get("output", ""))
                    perf_lines.extend(line for line in output_text.splitlines() if line.startswith("perf:"))
                incoming.put(message)
        except BaseException as exc:
            incoming.put(exc)

    threading.Thread(target=read_messages, daemon=True).start()

    def send(command: str, arguments: dict[str, object]) -> int:
        nonlocal sequence
        sequence += 1
        process.stdin.write(dap_frame({
            "seq": sequence, "type": "request", "command": command, "arguments": arguments,
        }))
        process.stdin.flush()
        return sequence

    def wait_for(predicate, description: str, timeout: float = DEFAULT_TIMEOUT) -> dict[str, object]:
        for index, message in enumerate(backlog):
            if predicate(message):
                return backlog.pop(index)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                message = incoming.get(timeout=max(0.1, deadline - time.monotonic()))
            except queue.Empty:
                break
            if isinstance(message, BaseException):
                raise message
            if message is None:
                stderr = process.stderr.read().decode("utf-8", "replace") if process.stderr else ""
                raise RuntimeError(
                    f"debugpy closed while waiting for {description}: {stderr}; prior messages={backlog!r}"
                )
            if predicate(message):
                return message
            backlog.append(message)
        raise TimeoutError(f"timed out waiting for {description}; messages={backlog!r}")

    def response(request_seq: int, description: str) -> dict[str, object]:
        message = wait_for(
            lambda item: item.get("type") == "response" and item.get("request_seq") == request_seq,
            description,
        )
        if message.get("success") is not True:
            raise RuntimeError(f"{description} failed: {message!r}; prior messages={backlog!r}")
        return message

    initialize = send("initialize", {
        "clientID": "visualstudio", "adapterID": "python", "pathFormat": "path",
        "linesStartAt1": True, "columnsStartAt1": True,
        "supportsVariableType": True, "supportsRunInTerminalRequest": False,
    })
    initialize_response = response(initialize, "initialize")
    if initialize_response.get("body", {}).get("supportsEvaluateForHovers") is not True:
        raise RuntimeError(f"debugpy did not advertise hover evaluation: {initialize_response!r}")
    progress("initialize response")

    launch_env = dict(LAUNCH_CONFIG["env"])
    launch_env.update({
        "XLANG3_PYTHON_LIB": str(VS_CORE),
        "XLANG3_DEBUGPY_ROOT": str(VS_CORE / "debugpy"),
        "PYDEVD_USE_SYS_MONITORING": os.environ.get(
            "XLANG3_DEBUGPY_MONITORING",
            str(LAUNCH_CONFIG["env"]["PYDEVD_USE_SYS_MONITORING"]),
        ),
    })
    launch_arguments = {
        key: value for key, value in LAUNCH_CONFIG.items() if not key.startswith("$")
    }
    interpreter_arguments = [str(profile_interpreter), "-S"]
    if os.environ.get("XLANG3_DEBUGPY_PERF_COUNTERS") == "1":
        interpreter_arguments.insert(1, "--perf-counters")
    launch_arguments.update({
        "python": interpreter_arguments,
        "debugLauncherPython": str(ADAPTER_PYTHON),
        "program": str(PROGRAM),
        "cwd": str(ROOT),
        "stopOnEntry": bool(LAUNCH_CONFIG.get("stopOnEntry", False)),
        "env": launch_env,
    })
    launch = send("launch", launch_arguments)
    wait_for(lambda item: item.get("type") == "event" and item.get("event") == "initialized", "initialized event")
    progress("initialized event")
    breakpoints = send("setBreakpoints", {
        "source": {"name": PROGRAM.name, "path": str(PROGRAM)},
        "breakpoints": [{"line": 17}, {"line": 18}],
        "sourceModified": False,
    })
    breakpoint_response = response(breakpoints, "setBreakpoints")
    actual_breakpoints = breakpoint_response.get("body", {}).get("breakpoints", [])
    if len(actual_breakpoints) != 2 or any(item.get("verified") is not True for item in actual_breakpoints):
        raise RuntimeError(f"debugpy did not verify the XLang3 source breakpoint: {breakpoint_response!r}")
    configured = send("configurationDone", {})
    response(configured, "configurationDone")
    progress("configurationDone response")
    response(launch, "launch")
    progress("launch response")

    stopped = wait_for(lambda item: item.get("type") == "event" and item.get("event") == "stopped", "debugger stop")
    progress(f"stopped: {stopped.get('body', {}).get('reason')}")
    if stopped.get("body", {}).get("reason") == "entry":
        entry_thread_id = int(stopped.get("body", {}).get("threadId", 0))
        if entry_thread_id <= 0:
            raise RuntimeError(f"entry stop has no thread: {stopped!r}")
        response(send("continue", {"threadId": entry_thread_id}), "continue from entry")
        stopped = wait_for(
            lambda item: item.get("type") == "event" and item.get("event") == "stopped",
            "source breakpoint after entry",
        )
        progress(f"stopped: {stopped.get('body', {}).get('reason')}")
    if stopped.get("body", {}).get("reason") != "breakpoint":
        raise RuntimeError(f"expected XLang3 source breakpoint: {stopped!r}")
    thread_id = int(stopped.get("body", {}).get("threadId", 0))
    if thread_id <= 0:
        raise RuntimeError(f"stopped event has no thread: {stopped!r}")
    stack = response(send("stackTrace", {"threadId": thread_id}), "stackTrace")
    progress("stackTrace response")
    frames = stack.get("body", {}).get("stackFrames", [])
    frame = next((item for item in frames if same_path(Path(item.get("source", {}).get("path", "")), PROGRAM)), None)
    if frame is None:
        raise RuntimeError(f"XLang3 user frame missing: {frames!r}")
    scopes = response(send("scopes", {"frameId": frame["id"]}), "scopes")
    progress("scopes response")
    local_scope = next(item for item in scopes.get("body", {}).get("scopes", []) if item.get("name") == "Locals")
    variables = response(send("variables", {"variablesReference": local_scope["variablesReference"]}), "variables")
    progress("variables response")
    names = {item.get("name"): item.get("value") for item in variables.get("body", {}).get("variables", [])}
    if names.get("value") != "41":
        raise RuntimeError(f"XLang3 local value was not visible through debugpy: {names!r}")
    progress("local value inspected")

    for attempt in range(3):
        evaluated = response(send("evaluate", {
            "expression": "value",
            "frameId": frame["id"],
            "context": "hover",
        }), f"hover evaluate {attempt + 1}")
        if evaluated.get("body", {}).get("result") != "41":
            raise RuntimeError(f"XLang3 hover evaluation returned an unexpected value: {evaluated!r}")
    progress("hover value evaluated repeatedly")

    response(send("continue", {"threadId": thread_id}), "continue")
    second_stopped = wait_for(
        lambda item: item.get("type") == "event" and item.get("event") == "stopped",
        "second source breakpoint",
    )
    if second_stopped.get("body", {}).get("reason") != "breakpoint":
        raise RuntimeError(f"expected second XLang3 source breakpoint: {second_stopped!r}")
    thread_id = int(second_stopped.get("body", {}).get("threadId", 0))
    second_stack = response(send("stackTrace", {"threadId": thread_id}), "second stackTrace")
    second_frame = next(
        (item for item in second_stack.get("body", {}).get("stackFrames", [])
         if same_path(Path(item.get("source", {}).get("path", "")), PROGRAM)),
        None,
    )
    if second_frame is None:
        raise RuntimeError(f"second breakpoint omitted the user frame: {second_stack!r}")
    second_evaluated = response(send("evaluate", {
        "expression": "value",
        "frameId": second_frame["id"],
        "context": "hover",
    }), "hover evaluate at second breakpoint")
    if second_evaluated.get("body", {}).get("result") != "41":
        raise RuntimeError(f"XLang3 repeated-stop hover returned an unexpected value: {second_evaluated!r}")
    progress("hover value evaluated at second breakpoint")
    response(send("continue", {"threadId": thread_id}), "continue from second breakpoint")
    pause_request = send("pause", {"threadId": thread_id})
    response(pause_request, "pause")
    paused = wait_for(
        lambda item: item.get("type") == "event" and item.get("event") == "stopped",
        "asynchronous pause",
    )
    if paused.get("body", {}).get("reason") != "pause":
        raise RuntimeError(f"expected asynchronous pause: {paused!r}")
    pause_thread_id = int(paused.get("body", {}).get("threadId", 0))
    pause_stack = response(send("stackTrace", {"threadId": pause_thread_id}), "pause stackTrace")
    pause_frames = pause_stack.get("body", {}).get("stackFrames", [])
    if not pause_frames:
        raise RuntimeError(f"asynchronous pause returned no stack frames: {pause_stack!r}")
    pause_user_frame = next(
        (item for item in pause_frames if same_path(Path(item.get("source", {}).get("path", "")), PROGRAM)),
        None,
    )
    if pause_user_frame is None:
        raise RuntimeError(f"asynchronous pause omitted the user frame: {pause_frames!r}")
    pause_evaluated = response(send("evaluate", {
        "expression": "value",
        "frameId": pause_user_frame["id"],
        "context": "hover",
    }), "hover evaluate after second stop")
    if pause_evaluated.get("body", {}).get("result") != "41":
        raise RuntimeError(f"XLang3 second-stop hover returned an unexpected value: {pause_evaluated!r}")
    progress("asynchronous pause and stackTrace response")
    response(send("continue", {"threadId": pause_thread_id}), "continue after pause")
    output = wait_for(
        lambda item: item.get("type") == "event" and item.get("event") == "output" and "42" in item.get("body", {}).get("output", ""),
        "program output",
    )
    wait_for(lambda item: item.get("type") == "event" and item.get("event") == "terminated", "terminated event")
    progress("program output and terminated event")
    response(send("disconnect", {"terminateDebuggee": False}), "disconnect")
    process.stdin.close()
    try:
        return_code = process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        # Visual Studio owns the stdio adapter lifetime. Once the DAP session
        # has terminated and disconnect has succeeded, stop a lingering
        # adapter exactly as the IDE does when it tears down its debug host.
        subprocess.run(
            ["taskkill", "/PID", str(process.pid), "/T", "/F"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False,
        )
        return_code = process.wait(timeout=10)
        progress("adapter stopped after completed DAP disconnect")
    stderr = process.stderr.read().decode("utf-8", "replace") if process.stderr else ""
    if os.environ.get("XLANG3_DEBUGPY_PERF_COUNTERS") == "1":
        for line in perf_lines:
            print(line)
    if return_code not in (0, 1):
        raise RuntimeError(f"debugpy adapter exited with {return_code}: {stderr}")
    runtime_name = "reference CPython" if REFERENCE_RUN else "XLang3"
    print(f"Visual Studio debugpy launched {runtime_name}, stopped, inspected locals, paused, continued, and exited")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    finally:
        if ADAPTER_PROCESS is not None and ADAPTER_PROCESS.poll() is None:
            subprocess.run(
                ["taskkill", "/PID", str(ADAPTER_PROCESS.pid), "/T", "/F"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=False,
            )
