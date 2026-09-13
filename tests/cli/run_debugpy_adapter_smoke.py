"""Exercise Visual Studio's unmodified debugpy adapter over DAP stdio."""

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
XLANG = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else ROOT / "build" / "Release" / "xlang3.exe"
VS_CORE = Path(
    r"C:\Program Files\Microsoft Visual Studio\18\Community"
    r"\Common7\IDE\Extensions\Microsoft\Python\Core"
)


def dap_frame(message: dict[str, object]) -> bytes:
    body = json.dumps(message, separators=(",", ":")).encode("utf-8")
    return b"Content-Length: " + str(len(body)).encode("ascii") + b"\r\n\r\n" + body


def main() -> int:
    if not (VS_CORE / "debugpy" / "adapter" / "__main__.py").is_file():
        print("debugpy adapter smoke skipped: Visual Studio debugpy is unavailable")
        return 0

    env = os.environ.copy()
    env["XLANG3_PYTHON_LIB"] = str(VS_CORE)
    env["XLANG3_DEBUGPY_ROOT"] = str(VS_CORE / "debugpy")
    env["PYTHONPATH"] = str(VS_CORE)
    bootstrap_source = ROOT / "tests" / "fixtures" / "core" / "debugpy_bootstrap_compat.py"
    bootstrap_expected = (
        ROOT / "tests" / "fixtures" / "expected" / "debugpy_bootstrap_compat.out"
    ).read_text(encoding="utf-8").replace("\r\n", "\n").rstrip()
    bootstrap = subprocess.run(
        [str(XLANG), str(bootstrap_source)],
        cwd=ROOT,
        env=env,
        text=True,
        capture_output=True,
    )
    bootstrap_actual = bootstrap.stdout.replace("\r\n", "\n").rstrip()
    if bootstrap.returncode != 0 or bootstrap_actual != bootstrap_expected:
        raise RuntimeError(
            "debugpy bootstrap compatibility fixture failed: "
            f"exit={bootstrap.returncode}; stdout={bootstrap.stdout!r}; stderr={bootstrap.stderr!r}"
        )
    process = subprocess.Popen(
        [str(XLANG), "-m", "debugpy.adapter"],
        cwd=ROOT,
        env=env,
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    assert process.stdin is not None and process.stdout is not None
    messages: queue.Queue[dict[str, object] | BaseException | None] = queue.Queue()

    def read_messages() -> None:
        try:
            while True:
                headers: dict[bytes, bytes] = {}
                while True:
                    line = process.stdout.readline()
                    if not line:
                        messages.put(None)
                        return
                    line = line.rstrip(b"\r\n")
                    if not line:
                        break
                    key, value = line.split(b":", 1)
                    headers[key.lower()] = value.strip()
                length = int(headers[b"content-length"])
                messages.put(json.loads(process.stdout.read(length)))
        except BaseException as exc:
            messages.put(exc)

    threading.Thread(target=read_messages, daemon=True).start()
    process.stdin.write(
        dap_frame(
            {
                "seq": 1,
                "type": "request",
                "command": "initialize",
                "arguments": {
                    "clientID": "xlang3-smoke",
                    "adapterID": "python",
                    "pathFormat": "path",
                    "linesStartAt1": True,
                    "columnsStartAt1": True,
                },
            }
        )
    )
    process.stdin.flush()

    seen_events: set[str] = set()
    initialized = False
    disconnected = False
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline and not initialized:
        message = messages.get(timeout=max(0.1, deadline - time.monotonic()))
        if isinstance(message, BaseException):
            raise message
        if message is None:
            raise RuntimeError("debugpy adapter closed before initialize response")
        if message.get("type") == "event":
            seen_events.add(str(message.get("event")))
        if message.get("type") == "response" and message.get("request_seq") == 1:
            if message.get("success") is not True:
                raise RuntimeError(f"initialize failed: {message!r}")
            initialized = True

    process.stdin.write(
        dap_frame(
            {
                "seq": 2,
                "type": "request",
                "command": "disconnect",
                "arguments": {"terminateDebuggee": False},
            }
        )
    )
    process.stdin.flush()
    while time.monotonic() < deadline and not disconnected:
        message = messages.get(timeout=max(0.1, deadline - time.monotonic()))
        if isinstance(message, BaseException):
            raise message
        if message is None:
            raise RuntimeError("debugpy adapter closed before disconnect response")
        if message.get("type") == "event":
            seen_events.add(str(message.get("event")))
        if message.get("type") == "response" and message.get("request_seq") == 2:
            if message.get("success") is not True:
                raise RuntimeError(f"disconnect failed: {message!r}")
            disconnected = True

    # Give the request handler time to close its listener and channel before
    # signaling EOF, matching an IDE client that consumes the response first.
    time.sleep(0.2)
    process.stdin.close()
    return_code = process.wait(timeout=15)
    stderr = process.stderr.read().decode("utf-8", "replace") if process.stderr else ""
    if not initialized or not disconnected or "terminated" not in seen_events:
        raise RuntimeError(f"incomplete DAP lifecycle: events={seen_events!r}")
    if return_code != 0:
        raise RuntimeError(f"debugpy adapter exited with {return_code}: {stderr}")
    print("debugpy adapter DAP initialize/disconnect passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
