"""Separate differential probe; run CPython 3.14.7 first, then candidate."""
import sqlite3


class UnobservedParameters:
    def __len__(self):
        raise AssertionError("locked executemany must reject before len")

    def __getitem__(self, index):
        raise AssertionError("locked executemany must reject before getitem")


con = sqlite3.connect(":memory:")
con.execute("CREATE TABLE writes (x)")
for operation in ("execute", "fetchone", "fetchall", "next", "close", "executescript", "executemany"):
    cursor = con.cursor()
    events = []

    def reenter():
        try:
            if operation == "execute":
                cursor.execute("SELECT 99")
            elif operation == "fetchone":
                cursor.fetchone()
            elif operation == "fetchall":
                cursor.fetchall()
            elif operation == "next":
                next(cursor)
            elif operation == "close":
                cursor.close()
            elif operation == "executescript":
                cursor.executescript("SELECT 99;")
            else:
                cursor.executemany("INSERT INTO writes VALUES (?)", UnobservedParameters())
        except sqlite3.ProgrammingError as error:
            assert str(error) == "Recursive use of cursors not allowed."
            events.append(operation)
        else:
            raise AssertionError("recursive cursor operation was accepted: " + operation)
        return 37

    con.create_function("reenter", 0, reenter)
    assert cursor.execute("SELECT reenter() AS result") is cursor
    assert events == [operation]
    assert tuple(cursor.fetchone()) == (37,)
    assert cursor.fetchone() is None
    assert cursor.description[0][0] == "result"
    cursor.close()
    print("cursor-guard-" + operation)
assert con.execute("SELECT count(*) FROM writes").fetchone()[0] == 0
con.close()
