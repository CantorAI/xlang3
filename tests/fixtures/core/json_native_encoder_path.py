# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0
# You may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Keep the pure-Python json API on XLang's own _json accelerator for the
# built-in payloads used by pyperformance. The tripwire proves these calls
# don't silently fall back to json.encoder._make_iterencode.
import json
import json.encoder
import _json

native_make_encoder = _json.make_encoder
native_factory_calls = 0


def counted_native_factory(*args, **kwargs):
    global native_factory_calls
    native_factory_calls += 1
    return native_make_encoder(*args, **kwargs)


def unexpected_python_fallback(*args, **kwargs):
    raise AssertionError("built-in json.dumps unexpectedly used _make_iterencode")


_json.make_encoder = counted_native_factory
json.encoder.c_make_encoder = counted_native_factory
json.encoder._make_iterencode = unexpected_python_fallback

empty = {}
simple = {"key1": 0, "key2": True, "key3": "value", "key4": "foo", "key5": "string"}
nested = {
    "key1": 0,
    "key2": simple,
    "key3": "value",
    "key4": simple,
    "key5": simple,
    "key": "\u0105\u0107\u017c",
}
huge = [nested] * 1000

results = [json.dumps(item) for item in (empty, simple, nested, huge)]
assert len(results) == 4
assert results[0] == "{}"
assert results[1].startswith('{"key1": 0,')
assert results[2].endswith('"key": "\\u0105\\u0107\\u017c"}')
assert len(results[3]) > 300_000
assert native_factory_calls == len(results)
print("native encoder calls", native_factory_calls)
print("pyperformance huge output bytes", len(results[3]))
