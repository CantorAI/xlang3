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
import sqlite3

assert sqlite3.dbapi2.__name__ == "sqlite3.dbapi2"
direct_connection = sqlite3.connect(":memory:")
assert direct_connection.execute("SELECT 42").fetchone()[0] == 42
direct_connection.execute("CREATE TABLE generated (id INTEGER PRIMARY KEY, value TEXT)")
generated_cursor = direct_connection.execute(
    "INSERT INTO generated (value) VALUES (?)", ("value",)
)
assert generated_cursor.lastrowid == 1
assert generated_cursor.rowcount == 1
script_cursor = direct_connection.executescript(
    "CREATE TABLE scripted (value INTEGER);"
    "INSERT INTO scripted VALUES (7);"
    "INSERT INTO scripted VALUES (9);"
)
assert isinstance(script_cursor, sqlite3.Cursor)
assert direct_connection.execute("SELECT sum(value) FROM scripted").fetchone()[0] == 16
cursor_script = direct_connection.cursor()
assert cursor_script.executescript(
    "INSERT INTO scripted VALUES (11);"
) is cursor_script
assert direct_connection.execute("SELECT count(*) FROM scripted").fetchone()[0] == 3
many_cursor = direct_connection.executemany(
    "INSERT INTO scripted VALUES (?)", [(13,), (15,)]
)
assert isinstance(many_cursor, sqlite3.Cursor)
assert many_cursor.rowcount == 2
assert many_cursor.lastrowid is None
assert direct_connection.execute("SELECT sum(value) FROM scripted").fetchone()[0] == 55
iter_cursor = direct_connection.execute("SELECT value FROM scripted ORDER BY value")
assert [row[0] for row in iter_cursor] == [7, 9, 11, 13, 15]
direct_connection.close()

blob_connection = sqlite3.connect(":memory:")
blob_cursor = blob_connection.cursor()
for payload in [b"", b"\x00\xff\x80binary\x00", b"\x00\xff" * 32768]:
    blob_cursor.execute("SELECT ?, typeof(?), length(?)", [payload, payload, payload])
    blob_row = blob_cursor.fetchone()
    assert isinstance(blob_row[0], bytes)
    assert blob_row[0] == payload
    assert blob_row[1] == "blob"
    assert blob_row[2] == len(payload)
blob_cursor.close()
blob_connection.close()

print(sqlite3.OperationalError("manual sqlite error"))

conn = sqlite3.connect(":memory:")
cur = conn.cursor()

print(cur.execute("CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, score REAL, note TEXT)"))
print(cur.execute("INSERT INTO users (id, name, score, note) VALUES (?, ?, ?, ?)", [1, "Ada", 98.5, None]))
print(cur.execute("INSERT INTO users (id, name, score, note) VALUES (?, ?, ?, ?)", [2, "Linus", 87.25, "kernel"]))
print(conn.commit())

print(cur.execute("SELECT id, name, score, note FROM users WHERE id = ?", [1]))
one = cur.fetchone()
print(one[0])
print(one[1])
print(one[2])
print(one[3])

print(cur.execute("SELECT id, name, score, note FROM users ORDER BY id"))
rows = cur.fetchall()
print(len(rows))
print(rows[0][1])
print(rows[1][3])

cur2 = conn.cursor()
print(cur2.execute("SELECT name FROM users WHERE score > ? ORDER BY id", [90]))
rows2 = cur2.fetchall()
print(len(rows2))
print(rows2[0][0])
print(cur2.close())

print(cur.execute("INSERT INTO users (id, name, score, note) VALUES (?, ?, ?, ?)", [3, "Grace", 100.0, "compiler"]))
print(conn.rollback())
print(cur.execute("SELECT name FROM users WHERE id = ?", [3]))
print(cur.fetchone())

print(cur.close())
print(conn.close())

with sqlite3.connect(":memory:") as scoped:
    with scoped.cursor() as scoped_cur:
        print(scoped_cur.execute("CREATE TABLE scoped (value INTEGER)"))
        print(scoped_cur.execute("INSERT INTO scoped (value) VALUES (?)", [7]))

check = scoped.cursor()
print(check.execute("SELECT value FROM scoped"))
print(check.fetchone()[0])
print(check.close())
print(scoped.close())

try:
    with sqlite3.connect(":memory:") as failing:
        with failing.cursor() as failing_cur:
            failing_cur.execute("SELECT * FROM missing_table")
except sqlite3.OperationalError as err:
    print("sqlite error caught")
    print(err)

try:
    with sqlite3.connect(":memory:") as failing_base:
        with failing_base.cursor() as failing_base_cur:
            failing_base_cur.execute("SELECT * FROM another_missing_table")
except sqlite3.Error as err:
    print("sqlite base error caught")
    print(err)
