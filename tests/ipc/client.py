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
