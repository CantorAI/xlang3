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

# A data-only package directory must not shadow the native library.
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "namespace_collision"))

import json
from xlang_json import json as prefixed_json
from xlang_yaml import yaml
import sqlite3
from xlang_sqlite3 import sqlite

print(json.loads('{"name":"xlang3"}')["name"])
print(prefixed_json.loads('{"value":42}')["value"])
print(yaml.loads("enabled: true\n")["enabled"])

conn = sqlite3.connect(":memory:")
cur = conn.cursor()
print(cur.execute("CREATE TABLE t(value INTEGER)"))
print(cur.execute("INSERT INTO t(value) VALUES (?)", [5]))
print(cur.execute("SELECT value FROM t"))
print(cur.fetchone()[0])
print(cur.close())
print(conn.close())

db = sqlite.Database(":memory:")
print(db.exec("CREATE TABLE old(value INTEGER)") == sqlite.OK)
print(db.close())
