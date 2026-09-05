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
if len(sys.argv) > 1:
    sys.path.insert(0, sys.argv[1])
from xlang_sqlite3 import sqlite

print(sqlite.OK)
print(sqlite.ROW)
print(sqlite.DONE)

db = sqlite.Database(":memory:")
print(db.exec("CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, score REAL, note TEXT)"))

insert = db.statement("INSERT INTO users (id, name, score, note) VALUES (?, ?, ?, ?)")
print(insert.bind(1, 1))
print(insert.bind(2, "Ada"))
print(insert.bind(3, 98.5))
print(insert.bind(4, None))
print(insert.step() == sqlite.DONE)
print(insert.reset())
print(insert.bind(1, 2))
print(insert.bind(2, "Linus"))
print(insert.bind(3, 87.25))
print(insert.bind(4, "kernel"))
print(insert.step() == sqlite.DONE)
print(insert.close())

stmt = db.statement("SELECT id, name, score, note FROM users ORDER BY id")
print(stmt.colnum())
print(stmt.colname(1))

while stmt.step() == sqlite.ROW:
    print(stmt.get(0))
    print(stmt.get(1))
    print(stmt.get(2))
    print(stmt.get(3))

print(stmt.step() == sqlite.DONE)
print(stmt.reset())

rows = stmt.fetchall()
print(len(rows))
print(rows[0][1])
print(rows[1][3])

print(stmt.reset())
dict_rows = stmt.fetchallDict()
print(len(dict_rows))
print(dict_rows[0]["name"])
print(dict_rows[1]["score"])

print(stmt.close())
print(db.beginTransaction())
print(db.exec("INSERT INTO users (id, name, score, note) VALUES (3, 'Grace', 100.0, 'compiler')"))
print(db.endTransaction())

check = db.statement("SELECT name FROM users WHERE id = 3")
print(check.step() == sqlite.ROW)
print(check.get(0))
print(check.close())
print(db.close())
