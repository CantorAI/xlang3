# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import io
import sys

read_empty = sys.stdin.read(0)
readline_empty = sys.stdin.readline(0)
print(type(read_empty).__name__, repr(read_empty))
print(type(readline_empty).__name__, repr(readline_empty))
print(type(sys.stdin).__module__, type(sys.stdin).__qualname__, sys.stdin.mode, sys.stdin.name)
print(sys.stdin.read.__text_signature__, sys.stdin.readline.__text_signature__)
print(sys.stdout.write.__text_signature__, sys.stdout.flush.__text_signature__, sys.stderr.fileno.__text_signature__)
print(sys.stdin.read.__doc__ is None, sys.stdout.write.__doc__ is None)
print(type(sys.stdin.buffer).__module__, type(sys.stdin.buffer).__qualname__, sys.stdin.buffer is sys.stdin)
print(type(sys.stdout.buffer).__module__, type(sys.stdout.buffer).__qualname__, sys.stdout.buffer is sys.stdout)
print(type(sys.stdin.buffer.read(0)).__name__, repr(sys.stdin.buffer.read(0)))
print(sys.stdout.buffer.write(b""), sys.stderr.buffer.write(bytearray(b"")))
print(sys.stdin.buffer.read.__text_signature__, sys.stdout.buffer.write.__text_signature__)
print(
    "unsupported-operation-metadata",
    io.UnsupportedOperation.__module__,
    io.UnsupportedOperation.__qualname__,
    issubclass(io.UnsupportedOperation, OSError),
    issubclass(io.UnsupportedOperation, ValueError),
)

def exc_message(func):
    try:
        func()
    except TypeError as err:
        return str(err)
    return "missing"

print(
    "buffer-keyword-diagnostics",
    exc_message(lambda: sys.stdin.buffer.read(size=0)) == "BufferedReader.read() takes no keyword arguments",
    exc_message(lambda: sys.stdout.buffer.write(buffer=b"")) == "BufferedWriter.write() takes no keyword arguments",
    exc_message(lambda: sys.stdin.buffer.readable(x=1)) == "BufferedReader.readable() takes no keyword arguments",
)
print(
    "buffer-noarg-diagnostics",
    exc_message(lambda: sys.stdin.buffer.readable(1)) == "BufferedReader.readable() takes no arguments (1 given)",
    exc_message(lambda: sys.stdout.buffer.flush(1)) == "BufferedWriter.flush() takes no arguments (1 given)",
    exc_message(lambda: sys.stderr.buffer.fileno(1)) == "BufferedWriter.fileno() takes no arguments (1 given)",
)

def exc_detail(func):
    try:
        result = func()
    except Exception as err:
        return type(err).__module__, type(err).__name__, str(err)
    return "OK", type(result).__name__, repr(result)

print(
    "buffer-capability",
    exc_detail(lambda: sys.stdin.buffer.write("x")) == ("io", "UnsupportedOperation", "write"),
    exc_detail(lambda: sys.stdout.buffer.read(0)) == ("io", "UnsupportedOperation", "read"),
    exc_detail(lambda: sys.stderr.buffer.read(None)) == ("io", "UnsupportedOperation", "read"),
    exc_detail(lambda: sys.stdout.buffer.read("x")) == (
        "builtins",
        "TypeError",
        "'str' object cannot be interpreted as an integer",
    ),
    exc_detail(lambda: sys.stderr.buffer.readline(0)) == ("OK", "bytes", "b''"),
)
