# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

import sys
import time


class Box:
    def __init__(self, value):
        self._value = value

    def value(self):
        return self._value

    @property
    def current(self):
        return self._value


class IpcSmokeServer:
    Box = Box

    def __init__(self):
        self.pressure_counts = {}

    def pressure_echo(self, token, payload):
        count = self.pressure_counts.get(token, 0) + 1
        self.pressure_counts[token] = count
        time.sleep(0.01)
        return [count, payload]


    def echo(self, value):
        return "echo:" + value

    def add(self, left, right):
        return left + right

    def invoke_callback(self, callback, value):
        return callback(value)

    def bytes_len(self, value):
        return len(value)

    def bytes_echo(self, value):
        return value

    def make_bytes(self, size):
        return b"x" * size

    def mixed(self, none_value, bool_value, int_value, float_value, text, data, tup, arr, mp):
        return [
            none_value is None,
            bool_value,
            int_value + 1,
            float_value,
            text + "!",
            len(data),
            tup[1],
            arr[2],
            mp["k"],
        ]

    def size_len(self, value):
        return len(value)

    def size_echo(self, value):
        return value

    def get_len(self):
        return len


port = int(sys.argv[1])
ipc_srv = IpcSmokeServer()
ipc_srv.name = "ipc-smoke"

register_remote_object("ipc_srv", ipc_srv)
if len(sys.argv) > 2:
    sys.path.insert(0, sys.argv[2])
    import xlang1_compat_sample
    register_remote_object("expression_srv", xlang1_compat_sample)
lrpc_listen(port, False)

print("lrpc-ready:" + str(port))
sys.stdout.flush()

while True:
    time.sleep(1)
