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

import sys


def catch(call):
    try:
        result = call()
    except Exception as exc:
        return type(exc).__name__, str(exc)
    return type(result).__name__, repr(result)


for label, obj in (
    ("version", sys.version_info),
    ("flags", sys.flags),
    ("int", sys.int_info),
    ("float", sys.float_info),
    ("hash", sys.hash_info),
    ("thread", sys.thread_info),
):
    getnewargs = obj.__getnewargs__()
    reduced = obj.__reduce__()
    print("sys-structseq-getnewargs", label, isinstance(getnewargs, tuple), len(getnewargs), getnewargs[0] == tuple(obj))
    print("sys-structseq-reduce", label, reduced[0] is type(obj), reduced[1][0] == tuple(obj), isinstance(reduced[1][1], dict), len(reduced[1][1]))


flags_named = sys.flags.__reduce__()[1][1]
print(
    "sys-flags-reduce-named",
    flags_named["gil"],
    flags_named["thread_inherit_context"],
    flags_named["context_aware_warnings"],
    "gil" not in type(sys.flags).__match_args__,
)

for name in ("__getnewargs__", "__reduce__"):
    method = getattr(sys.flags, name)
    print(
        "sys-structseq-method-meta",
        name,
        method.__name__,
        method.__qualname__,
        method.__module__ is None,
        method.__doc__ is None,
        method.__text_signature__,
    )

print("sys-structseq-diagnostic", "getnewargs-extra", catch(lambda: sys.flags.__getnewargs__(1)))
print("sys-structseq-diagnostic", "getnewargs-keyword", catch(lambda: sys.flags.__getnewargs__(x=1)))
print("sys-structseq-diagnostic", "getnewargs-unbound-missing", catch(lambda: type(sys.flags).__getnewargs__()))
print("sys-structseq-diagnostic", "getnewargs-receiver", catch(lambda: type(sys.flags).__getnewargs__(object())))
print("sys-structseq-diagnostic", "reduce-extra", catch(lambda: sys.flags.__reduce__(1)))
print("sys-structseq-diagnostic", "reduce-keyword", catch(lambda: sys.flags.__reduce__(x=1)))
print("sys-structseq-diagnostic", "reduce-unbound-missing", catch(lambda: type(sys.flags).__reduce__()))
print("sys-structseq-diagnostic", "reduce-receiver", catch(lambda: type(sys.flags).__reduce__(object())))
