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

import os
import sys


PATH = "xlang3_dump_tracelets.tmp"
EXPECTED = 'digraph ideal {\n\n    rankdir = "LR"\n\n}\n\n'


class TextPath:
    def __fspath__(self):
        return PATH


class BytesPath:
    def __fspath__(self):
        return bytes(PATH, "utf-8")


class BadPath:
    def __fspath__(self):
        return 42


class RaisingPath:
    def __fspath__(self):
        raise RuntimeError("fspath-boom")


def catch(call):
    try:
        call()
    except Exception as exc:
        return type(exc).__name__, str(exc)
    return "no-error", ""


def dump_with(path):
    if os.path.exists(PATH):
        os.remove(PATH)
    result = sys._dump_tracelets(path)
    with open(PATH, "r") as file:
        data = file.read().replace("\r\n", "\n")
    print("dump-tracelets", result is None, os.path.exists(PATH), data == EXPECTED)
    os.remove(PATH)


dump_with(PATH)
dump_with(bytes(PATH, "utf-8"))
dump_with(TextPath())
dump_with(BytesPath())
print("dump-tracelets-bad-fspath", catch(lambda: sys._dump_tracelets(BadPath())))
print("dump-tracelets-raising-fspath", catch(lambda: sys._dump_tracelets(RaisingPath())))
