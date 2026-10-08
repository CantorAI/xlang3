"""Python aggregate used by the native ABI ownership test."""
import sqlite3


class Sum:
    def __init__(self):
        self.total = 0

    def step(self, value):
        self.total += value

    def finalize(self):
        return self.total


def connect():
    return sqlite3.connect(":memory:")


def query(connection):
    return connection.execute(
        "SELECT sdk_sum(x) FROM (SELECT 1 AS x UNION ALL SELECT 2 UNION ALL SELECT 3)"
    ).fetchone()[0]


def empty(connection):
    return connection.execute("SELECT sdk_sum(x) FROM (SELECT 1 AS x) WHERE 0").fetchone()[0]


def retain_and_fail(connection):
    # args explicitly retains the connection; no self-referencing error local
    # is introduced into its traceback. This makes delayed factory cleanup a
    # deterministic ownership test rather than depending on frame retention.
    raise RuntimeError(connection)
