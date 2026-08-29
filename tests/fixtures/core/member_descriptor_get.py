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
import time


def catch(call):
    try:
        result = call()
    except Exception as exc:
        return type(exc).__name__, str(exc)
    return type(result).__name__, repr(result)


version_major = type(sys.version_info).major
time_year = time.struct_time.tm_year
sample_time = time.struct_time((2026, 8, 29, 1, 2, 3, 5, 241, -1))

print("member-get-none-owner", version_major.__get__(None, type(sys.version_info)) is version_major)
print("member-get-version", version_major.__get__(sys.version_info), version_major.__get__(sys.version_info, type(sys.version_info)))
print("member-get-time", time_year.__get__(sample_time), time_year.__get__(sample_time, time.struct_time))
print("member-get-none-missing-owner", catch(lambda: version_major.__get__(None)))
print("member-get-none-none-owner", catch(lambda: version_major.__get__(None, None)))
print("member-get-wrong-receiver", catch(lambda: version_major.__get__((), type(sys.version_info))))
print("member-get-missing", catch(lambda: version_major.__get__()))
print("member-get-extra", catch(lambda: version_major.__get__(sys.version_info, type(sys.version_info), object)))
print("member-set-version-readonly", catch(lambda: version_major.__set__(sys.version_info, 9)))
print("member-set-time-readonly", catch(lambda: time_year.__set__(sample_time, 2027)))
print("member-set-wrong-receiver", catch(lambda: version_major.__set__((), 9)))
print("member-set-missing", catch(lambda: version_major.__set__()))
print("member-set-one", catch(lambda: version_major.__set__(sys.version_info)))
print("member-set-extra", catch(lambda: version_major.__set__(sys.version_info, 9, 10)))
print("member-delete-version-readonly", catch(lambda: version_major.__delete__(sys.version_info)))
print("member-delete-time-readonly", catch(lambda: time_year.__delete__(sample_time)))
print("member-delete-wrong-receiver", catch(lambda: version_major.__delete__(())))
print("member-delete-missing", catch(lambda: version_major.__delete__()))
print("member-delete-extra", catch(lambda: version_major.__delete__(sys.version_info, 1)))
