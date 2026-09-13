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
import inspect
import sys


def outer():
    return inner()


def inner():
    frame = inspect.currentframe()
    print(frame.f_back.f_code.co_name)
    print(frame.f_code.co_firstlineno)
    print("debug_frame_metadata.py" in frame.f_code.co_filename)


code = compile("x = 1", "virtual_file.py", "exec")
print(code.co_filename)
print(code.co_firstlineno)
outer()

frame = inspect.currentframe()
frame_attrs = (
    "f_back", "f_builtins", "f_code", "f_generator", "f_globals", "f_lasti",
    "f_lineno", "f_locals", "f_trace", "f_trace_lines", "f_trace_opcodes",
)
print(all(hasattr(frame, name) for name in frame_attrs))


def local_trace(frame, event, arg):
    return local_trace


frame.f_trace = local_trace
frame.f_trace_lines = False
frame.f_trace_opcodes = True
print(frame.f_trace is local_trace, frame.f_trace_lines, frame.f_trace_opcodes, frame.f_generator is None)

metadata_code = (lambda a, /, b=1, *args, c=2, **kwargs: 0).__code__
code_attrs = (
    "co_argcount", "co_branches", "co_cellvars", "co_code", "co_consts",
    "co_exceptiontable", "co_filename", "co_firstlineno", "co_flags",
    "co_freevars", "co_kwonlyargcount", "co_lines", "co_linetable",
    "co_lnotab", "co_name", "co_names", "co_nlocals", "co_positions",
    "co_posonlyargcount", "co_qualname", "co_stacksize", "co_varnames",
)
print(all(hasattr(metadata_code, name) for name in code_attrs))
renamed = metadata_code.replace(co_name="renamed", co_qualname="qualified")
print(renamed.co_name, renamed.co_qualname, metadata_code.co_name)
print(isinstance(metadata_code.co_linetable, bytes), isinstance(metadata_code.co_lnotab, bytes))
print(list(metadata_code.co_branches()))


def live_locals_refresh():
    value = 1
    frame = sys._getframe()
    value = 2
    return frame is sys._getframe(), frame.f_locals["value"]


print("live-frame-locals", live_locals_refresh())


def returned_frame_locals():
    value = 1
    frame = sys._getframe()
    value = 2
    return frame


returned_frame = returned_frame_locals()
print("returned-frame-locals", returned_frame.f_locals["value"])
