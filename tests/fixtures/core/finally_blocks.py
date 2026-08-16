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

try:
    print("normal body")
finally:
    print("normal finally")

try:
    try:
        print("raise body")
        raise RuntimeError("raise path")
    finally:
        print("raise finally")
except RuntimeError as err:
    print("caught raise")
    print(err)

try:
    try:
        print("handled body")
        raise TypeError("handled path")
    except TypeError as err:
        print("handled except")
        print(err)
    finally:
        print("handled finally")
except:
    print("wrong")

def return_path():
    try:
        print("return body")
        return 7
    finally:
        print("return finally")

print(return_path())

def nested_return_path():
    try:
        try:
            print("nested return body")
            return 9
        finally:
            print("inner finally")
    finally:
        print("outer finally")

print(nested_return_path())
