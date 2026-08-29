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
    reduced_ex = obj.__reduce_ex__(4)
    print("sys-structseq-getnewargs", label, isinstance(getnewargs, tuple), len(getnewargs), getnewargs[0] == tuple(obj))
    print("sys-structseq-reduce", label, reduced[0] is type(obj), reduced[1][0] == tuple(obj), isinstance(reduced[1][1], dict), len(reduced[1][1]))
    print("sys-structseq-reduce-ex", label, reduced_ex[0] is type(obj), reduced_ex[1][0] == tuple(obj), isinstance(reduced_ex[1][1], dict), len(reduced_ex[1][1]))


flags_named = sys.flags.__reduce__()[1][1]
flags_reduce_ex_named = sys.flags.__reduce_ex__(True)[1][1]
print(
    "sys-flags-reduce-named",
    flags_named["gil"],
    flags_named["thread_inherit_context"],
    flags_named["context_aware_warnings"],
    flags_reduce_ex_named["gil"],
    flags_reduce_ex_named["thread_inherit_context"],
    flags_reduce_ex_named["context_aware_warnings"],
    "gil" not in type(sys.flags).__match_args__,
)

for label, obj in (
    ("int", sys.int_info),
    ("float", sys.float_info),
    ("hash", sys.hash_info),
    ("thread", sys.thread_info),
):
    cls = type(obj)
    print(
        "sys-structseq-construct",
        label,
        tuple(cls(tuple(obj))) == tuple(obj),
        tuple(cls(list(obj))) == tuple(obj),
        tuple(cls(*obj.__reduce__()[1])) == tuple(obj),
        repr(cls(tuple(obj))).startswith("sys." + cls.__name__ + "("),
    )

print("sys-structseq-noninstantiable", "version", catch(lambda: type(sys.version_info)(tuple(sys.version_info))))
print("sys-structseq-noninstantiable", "flags", catch(lambda: type(sys.flags)(tuple(sys.flags))))
print("sys-structseq-construct-diagnostic", "missing", catch(lambda: type(sys.int_info)()))
print("sys-structseq-construct-diagnostic", "extra", catch(lambda: type(sys.int_info)(tuple(sys.int_info), {}, None)))
print("sys-structseq-construct-diagnostic", "short", catch(lambda: type(sys.int_info)(tuple(sys.int_info)[:-1])))
print("sys-structseq-construct-diagnostic", "long", catch(lambda: type(sys.int_info)(tuple(sys.int_info) + (0,))))
print("sys-structseq-construct-diagnostic", "dict-extra", catch(lambda: type(sys.int_info)(tuple(sys.int_info), {"extra": 1})))
print("sys-structseq-construct-keyword", tuple(type(sys.int_info)(sequence=tuple(sys.int_info))) == tuple(sys.int_info))
print("sys-structseq-construct-diagnostic", "duplicate-sequence", catch(lambda: type(sys.int_info)(tuple(sys.int_info), sequence=tuple(sys.int_info))))

for name in ("__getnewargs__", "__reduce__", "__reduce_ex__"):
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
print("sys-structseq-diagnostic", "reduce-ex-missing", catch(lambda: sys.flags.__reduce_ex__()))
print("sys-structseq-diagnostic", "reduce-ex-extra", catch(lambda: sys.flags.__reduce_ex__(4, 5)))
print("sys-structseq-diagnostic", "reduce-ex-keyword", catch(lambda: sys.flags.__reduce_ex__(protocol=4)))
print("sys-structseq-diagnostic", "reduce-ex-type", catch(lambda: sys.flags.__reduce_ex__("x")))
print("sys-structseq-diagnostic", "reduce-ex-unbound-missing", catch(lambda: type(sys.flags).__reduce_ex__()))
