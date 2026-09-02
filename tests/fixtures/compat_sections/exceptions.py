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

import builtins
import sys


# CPython-style built-in exception hierarchy.
print(issubclass(ZeroDivisionError, ArithmeticError), issubclass(OverflowError, ArithmeticError))
print(issubclass(ModuleNotFoundError, ImportError), issubclass(UnboundLocalError, NameError))
print(issubclass(TabError, IndentationError), issubclass(RecursionError, RuntimeError))
print(issubclass(UnicodeDecodeError, UnicodeError), issubclass(UnicodeEncodeError, UnicodeError))
print(issubclass(ExceptionGroup, Exception), issubclass(ExceptionGroup, BaseExceptionGroup))
print(issubclass(KeyboardInterrupt, BaseException), issubclass(KeyboardInterrupt, Exception))
print(issubclass(SystemExit, BaseException), issubclass(GeneratorExit, BaseException))


# Compatibility aliases and builtins module exports.
print(IOError is OSError, EnvironmentError is OSError, WindowsError is OSError)
print(builtins.FileExistsError is FileExistsError, builtins.EncodingWarning is EncodingWarning)


# Exception arguments and typed/subclass catching.
try:
    err = ValueError("left", "right")
    raise err
except ValueError as caught:
    print(caught.args[0], caught.args[1], caught.__cause__ is None)

try:
    raise FileNotFoundError("missing")
except OSError as caught:
    print("os", caught.args[0])


# Finally participates in unwind before the handler continues.
events = []
try:
    try:
        raise RuntimeError("fin")
    finally:
        events.append("finally")
except RuntimeError as caught:
    print(events[0], caught.args[0])


# sys.exc_info is active only while handling an exception.
print(sys.exc_info()[0] is None, sys.exc_info()[1] is None, sys.exc_info()[2] is None)

try:
    try:
        raise ValueError("inner")
    except ValueError as inner:
        info = sys.exc_info()
        print(info[0] is ValueError, info[1].args[0])
        raise RuntimeError("outer") from inner
except RuntimeError as outer:
    info = sys.exc_info()
    print(info[0] is RuntimeError, info[1].args[0], info[2] is None)
    print(outer.__cause__.args[0], outer.__context__.args[0], outer.__suppress_context__)

print(sys.exc_info()[0] is None, sys.exc_info()[1] is None, sys.exc_info()[2] is None)


# Implicit context and explicit suppression with "raise from None".
try:
    try:
        raise KeyError("ctx")
    except KeyError:
        raise TypeError("wrap")
except TypeError as wrapped:
    print(wrapped.__context__.args[0], wrapped.__cause__ is None, wrapped.__suppress_context__)

try:
    try:
        raise LookupError("hidden")
    except LookupError:
        raise RuntimeError("clean") from None
except RuntimeError as clean:
    print(clean.__context__.args[0], clean.__cause__ is None, clean.__suppress_context__)


# Bare raise reuses the active exception and is catchable outside active handling.
try:
    raise TypeError("again")
except TypeError:
    try:
        raise
    except TypeError as reraised:
        print("reraised", reraised.args[0])

try:
    raise
except RuntimeError as caught:
    print("bare-outside", caught.args[0])


# Traceback objects capture the VM frame chain.
def outer_frame():
    def inner_frame():
        raise LookupError("tb")

    inner_frame()


try:
    outer_frame()
except LookupError as caught:
    names = []
    tb = caught.__traceback__
    while tb is not None:
        names.append(tb.tb_frame.f_code.co_name)
        tb = tb.tb_next
    print("<module>" in names, "outer_frame" in names, "inner_frame" in names)


# Traceback line numbers align with function source metadata.
def line_probe():
    marker = 1
    raise LookupError("line")


try:
    line_probe()
except LookupError as caught:
    tb = caught.__traceback__
    while tb.tb_next is not None:
        tb = tb.tb_next
    print(tb.tb_lineno == line_probe.__code__.co_firstlineno + 2)


# OSError exposes CPython-compatible errno fields and remaps common errno codes.
plain_os_error = OSError("plain")
missing_os_error = OSError(2, "missing", "name.txt")
permission_os_error = OSError(13, "denied")
try:
    open("xlang3_exception_missing_file.tmp", "r")
except FileNotFoundError as missing_file:
    print(
        plain_os_error.errno is None,
        type(missing_os_error).__name__,
        missing_os_error.errno,
        missing_os_error.strerror,
        missing_os_error.filename,
        type(permission_os_error).__name__,
        missing_file.errno,
        missing_file.filename.endswith("xlang3_exception_missing_file.tmp"),
    )
