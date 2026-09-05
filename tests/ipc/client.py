# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

import sys


endpoint = "lrpc:" + sys.argv[1]

import ipc_srv as srv thru endpoint

print("client-start")
print(srv.name)
print(srv.echo("hello"))
print(srv.add(20, 22))
print(srv.bytes_len(b"\x00abc"))
print(srv.bytes_echo(b"\x00abc") == b"\x00abc")
print(srv.Box(7).value())
print(srv.get_len()("abcd"))

callback_values = []
def callback(value):
    callback_values.append(value)
    return srv.add(len(value), 1)

callback_payload = b"\x00\xff" * 70000
assert srv.invoke_callback(callback, callback_payload) == len(callback_payload) + 1
assert callback_values == [callback_payload], "nested callback lost its arguments"
def bad_callback(value):
    raise RuntimeError("callback failure marker")
try:
    srv.invoke_callback(bad_callback, None)
    raise AssertionError("callback exception was swallowed")
except RuntimeError as error:
    assert "callback failure marker" in str(error)
assert srv.add(20, 22) == 42, "server did not recover after callback exception"

assert srv.add(right=22, left=20) == 42, "keyword method call failed"
assert srv.add(20, right=22) == 42, "mixed arguments failed"
assert srv.Box(value=9).value() == 9, "keyword constructor failed"
if srv.Box(13).current != 13:
    raise RuntimeError("remote property getter was not evaluated")
try:
    srv.add(20, left=1, right=22)
    raise AssertionError("duplicate argument accepted")
except RuntimeError as error:
    assert "multiple values" in str(error), "unexpected remote error"
assert srv.bytes_echo(value=b"\x00" * 70000) == b"\x00" * 70000, "keyword bytes mismatch"

mixed = srv.mixed(None, True, 41, 2.5, "hi", b"\x00\x01", ("a", 9), [1, 2, 3], {"k": "v"})
print(mixed[0])
print(mixed[1])
print(mixed[2])
print(mixed[3])
print(mixed[4])
print(mixed[5])
print(mixed[6])
print(mixed[7])
print(mixed[8])

large_text = "x" * 70000
print(srv.size_len(large_text))
print(srv.size_echo(large_text) == large_text)

large_bytes = b"a" * 70000
print(srv.size_len(large_bytes))
print(srv.size_echo(large_bytes) == large_bytes)
