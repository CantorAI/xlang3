"""Real native SQLite cache resource/lease observations for the C++ fixture."""
import sqlite3


def connect(size=128):
    return sqlite3.connect(':memory:', cached_statements=size)


def setup(connection):
    connection.execute('CREATE TABLE cache_rows(x)')


def insert(connection, value):
    return connection.execute('INSERT INTO cache_rows VALUES (?)', (value,))


def scalar(connection, sql):
    return connection.execute(sql).fetchone()[0]


def rows(connection):
    return connection.execute('SELECT x FROM cache_rows ORDER BY rowid').fetchall()


def open_rows(connection):
    return connection.execute('SELECT 1 UNION ALL SELECT 2')


def bind_nested(connection):
    class Parameters:
        def __len__(self):
            return 1

        def __getitem__(self, index):
            assert index == 0
            insert(connection, 2)
            return 1

    return connection.execute('INSERT INTO cache_rows VALUES (?)', Parameters())


def failed_binding(connection):
    try:
        insert(connection, object())
    except sqlite3.Error:
        return True
    raise AssertionError('unsupported binding did not fail')


def omitted_binding(connection):
    # The accepted own binder's omitted-parameter behavior is an existing
    # compatibility gap. This C++-only observation proves cache reuse never
    # resurrects a previous non-NULL binding; it is not a CP reference fixture.
    connection.execute('INSERT INTO cache_rows VALUES (?)')
    return connection.execute('SELECT x FROM cache_rows ORDER BY rowid DESC').fetchone()[0]
