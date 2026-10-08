"""Native cursor progression fixture; capture exact CPython 3.14.7 first."""
import sqlite3

con = sqlite3.connect(":memory:")
calls = []


def mark(value):
    calls.append(value)
    return value


con.create_function("mark", 1, mark)
cur = con.execute("SELECT mark(x) AS marked FROM (SELECT 1 AS x UNION ALL SELECT 2 UNION ALL SELECT 3)")
assert calls == [1]
assert tuple(cur.fetchone()) == (1,) and calls == [1, 2]
assert [tuple(row) for row in cur.fetchall()] == [(2,), (3,)]
assert calls == [1, 2, 3]
print("cursor-initial-step-and-lookahead")
for unused in range(3):
    assert cur.fetchone() is None and cur.fetchall() == []
assert calls == [1, 2, 3]
assert cur.description == (("marked", None, None, None, None, None, None),)
empty = con.execute("SELECT 1 AS empty_column WHERE 0")
assert empty.fetchone() is None and empty.fetchall() == []
assert empty.description[0][0] == "empty_column"
print("cursor-exhaustion-description")


class Sum:
    def __init__(self):
        self.total = 0

    def step(self, value):
        self.total += value

    def finalize(self):
        return self.total


con.create_aggregate("own_sum", 1, Sum)
retained = con.execute("SELECT own_sum(x) AS total FROM (SELECT 1 AS x UNION ALL SELECT 2)")
assert tuple(retained.fetchone()) == (3,)
# Keep the cursor alive; the final fetch must already have finished SQL.
con.create_aggregate("own_sum", 1, Sum)
assert retained.fetchone() is None and retained.description[0][0] == "total"
print("cursor-retained-aggregate-replacement")
busy = con.execute("SELECT g, own_sum(x) FROM (SELECT 0 AS g, 1 AS x UNION ALL SELECT 1, 2) GROUP BY g ORDER BY g")
assert tuple(busy.fetchone()) == (0, 1)
try:
    con.create_aggregate("own_sum", 1, Sum)
except sqlite3.OperationalError:
    pass
else:
    raise AssertionError("unfinished SELECT must still block registration replacement")
assert tuple(busy.fetchone()) == (1, 2)
con.create_aggregate("own_sum", 1, Sum)
print("cursor-unfinished-query-stays-busy")

text = "first\x00text"
blob = b"\x00\xff"
rows = con.execute("SELECT ? AS payload UNION ALL SELECT ?", (text, blob))
first = rows.fetchone()
second = rows.fetchone()
assert first[0] == text and second[0] == blob
assert rows.fetchone() is None and first[0] == text and second[0] == blob
print("cursor-row-buffer-ownership")


def fail_second(value):
    if value == 2:
        raise ValueError("lookahead failure")
    return value


con.create_function("fail_second", 1, fail_second)
error_cursor = con.execute("SELECT fail_second(x) FROM (SELECT 1 AS x UNION ALL SELECT 2)")
try:
    error_cursor.fetchone()
except sqlite3.OperationalError:
    pass
else:
    raise AssertionError("lookahead failure must discard the copied current row")
assert error_cursor.fetchone() is None and error_cursor.fetchall() == []
assert tuple(error_cursor.execute("SELECT 7 AS recovered").fetchone()) == (7,)
print("cursor-lookahead-error-cleanup")
try:
    con.execute("SELECT fail_second(2)")
except sqlite3.OperationalError:
    pass
else:
    raise AssertionError("execute must perform the initial step")
print("cursor-execute-error-timing")

con.execute("CREATE TABLE returning_rows (x INTEGER)")
returning = con.execute("INSERT INTO returning_rows VALUES (8), (9) RETURNING x")
assert returning.rowcount == 0
assert tuple(returning.fetchone()) == (8,) and returning.rowcount == 0
assert tuple(returning.fetchone()) == (9,) and returning.rowcount == 2
assert returning.fetchone() is None and returning.description[0][0] == "x"
print("cursor-returning-completion")
con.close()
