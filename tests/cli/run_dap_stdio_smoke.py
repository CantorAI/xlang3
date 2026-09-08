# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

import json
import subprocess
import sys


def frame(message):
    payload = json.dumps(message, separators=(",", ":")).encode()
    return f"Content-Length: {len(payload)}\r\n\r\n".encode() + payload


def read_frames(data):
    messages = []
    while data:
        header, separator, rest = data.partition(b"\r\n\r\n")
        if not separator:
            break
        fields = dict(line.split(b":", 1) for line in header.split(b"\r\n"))
        size = int(fields[b"Content-Length"].strip())
        messages.append(json.loads(rest[:size]))
        data = rest[size:]
    return messages


def main():
    requests = [
        {"seq": 1, "type": "request", "command": "initialize", "arguments": {}},
        {"seq": 2, "type": "request", "command": "launch", "arguments": {
            "program": "dap_stdio_smoke.py", "source": "print(42)\n"}},
        {"seq": 3, "type": "request", "command": "setExceptionBreakpoints", "arguments": {"filters": []}},
        {"seq": 4, "type": "request", "command": "configurationDone", "arguments": {}},
        {"seq": 5, "type": "request", "command": "threads", "arguments": {}},
        {"seq": 6, "type": "request", "command": "disconnect", "arguments": {}},
    ]
    result = subprocess.run(
        [sys.argv[1], "--dap-stdio"],
        input=b"".join(frame(request) for request in requests),
        capture_output=True,
        timeout=30,
    )
    if result.returncode:
        raise RuntimeError(f"DAP process failed ({result.returncode}): {result.stderr.decode()}")
    messages = read_frames(result.stdout)

    def find(kind, name):
        key = "command" if kind == "response" else "event"
        return next((item for item in messages if item.get("type") == kind and item.get(key) == name), None)

    for command in ("initialize", "launch", "setExceptionBreakpoints", "threads"):
        response = find("response", command)
        if not response or not response.get("success"):
            raise RuntimeError(f"missing successful {command} response")
    threads = find("response", "threads")
    if threads["body"]["threads"][0]["id"] != 1:
        raise RuntimeError("wrong DAP thread id")
    if not find("event", "initialized") or not find("event", "terminated"):
        raise RuntimeError("missing DAP lifecycle event")
    output = find("event", "output")
    if not output or output["body"]["output"] != "42\n":
        raise RuntimeError("missing DAP output event")


if __name__ == "__main__":
    main()
