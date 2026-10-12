"""Proposed core fixture. Not executed; capture CPython 3.14.7 first."""
import sqlite3
import warnings
import weakref

con = sqlite3.connect(":memory:")
con.execute("CREATE TABLE input (g INTEGER, x)")
con.executemany("INSERT INTO input VALUES (?, ?)", [(0, 1), (1, 5), (0, 2), (0, 3)])
# Keep setup rows independent of callback-error transaction rollback.
con.commit()
events = []


class Sum:
    def __init__(self):
        self.total = 0
        events.append("init")

    def step(self, value):
        self.total += value

    def finalize(self):
        events.append("final")
        return self.total


con.create_aggregate("own_sum", 1, Sum)
assert [tuple(row) for row in con.execute("SELECT g, own_sum(x) FROM input GROUP BY g ORDER BY g")] == [(0, 6), (1, 5)]
assert events.count("init") == events.count("final") == 2
events.clear()
row = con.execute("SELECT own_sum(x), own_sum(x+1) FROM input").fetchone()
assert tuple(row) == (11, 15)
assert events.count("init") == events.count("final") == 2
events.clear()
assert con.execute("SELECT own_sum(x) FROM input WHERE 0").fetchone()[0] is None
assert events == []
con.create_aggregate("empty_factory", 1, None)
assert con.execute("SELECT empty_factory(x) FROM input WHERE 0").fetchone()[0] is None
print("aggregate-groups-empty")


class Variadic:
    def __init__(self):
        self.count = 0

    def step(self, *values):
        self.count += len(values)

    def finalize(self):
        return self.count


con.create_aggregate("arity", -1, Variadic)
assert con.execute("SELECT arity(x,g,NULL) FROM input").fetchone()[0] == 12
con.create_aggregate("arity_zero", 0, Variadic)
assert con.execute("SELECT arity_zero() FROM input").fetchone()[0] == 0
print("aggregate-arity")


def later_step(self, value):
    self.total += 10 * value


class Changing:
    def __init__(self):
        self.total = 0

    def step(self, value):
        self.total += value
        Changing.step = later_step
        Changing.finalize = lambda self: self.total * 2

    def finalize(self):
        return -1


con.create_aggregate("changing", 1, Changing)
assert con.execute("SELECT changing(x) FROM (SELECT 1 AS x UNION ALL SELECT 2)").fetchone()[0] == 42


class DynamicDescriptor:
    def __init__(self):
        self.total = 0

    @property
    def step(self):
        def accept(value):
            self.total += value
        return accept

    @property
    def finalize(self):
        return lambda: self.total


con.create_aggregate("descriptor_sum", 1, DynamicDescriptor)
assert con.execute("SELECT descriptor_sum(x) FROM input").fetchone()[0] == 11
print("aggregate-dynamic-dispatch")


class Echo:
    def step(self, value):
        self.value = value

    def finalize(self):
        return self.value


con.create_aggregate("echo", 1, Echo)
for value in [None, -2**63, 2**63-1, 1.25, "nul\x00text", "非ASCII", b"", b"\x00\xff"]:
    result = con.execute("SELECT echo(?)", (value,)).fetchone()[0]
    assert result == value and type(result) is type(value)


result_value = None


class ReturnValue:
    def step(self):
        pass

    def finalize(self):
        return result_value


con.create_aggregate("result_value", 0, ReturnValue)
for value in [True, bytearray(b"a\x00b"), memoryview(b"c\x00d"), memoryview(b"")]:
    result_value = value
    actual = con.execute("SELECT result_value()").fetchone()[0]
    expected = int(value) if isinstance(value, bool) else bytes(value)
    assert actual == expected


class IntResult(int):
    def __index__(self):
        raise AssertionError("SQLite must read the int payload")


class FloatResult(float):
    def __float__(self):
        raise AssertionError("SQLite must read the float payload")


class StringResult(str):
    def __str__(self):
        raise AssertionError("SQLite must read the string payload")


class BytesResult(bytes):
    def __bytes__(self):
        raise AssertionError("SQLite must read the bytes payload")


for value, expected in [(IntResult(7), 7), (FloatResult(1.5), 1.5),
                        (StringResult("text"), "text"), (BytesResult(b"bytes"), b"bytes")]:
    result_value = value
    assert con.execute("SELECT result_value()").fetchone()[0] == expected
print("aggregate-value-conversion")


def expect_error(klass, name, factory, phrase=None):
    con.create_aggregate(name, 1, factory)
    try:
        con.execute("SELECT " + name + "(x) FROM input").fetchone()
    except klass as error:
        if phrase is not None:
            assert str(error) == phrase, str(error)
    else:
        raise AssertionError(name + " did not fail")
    assert con.execute("SELECT 42").fetchone()[0] == 42


class InitError:
    def __init__(self):
        raise ValueError("init payload")


class NoStep:
    def finalize(self):
        return 1


class NoFinalize:
    def step(self, value):
        pass


class StepError(Sum):
    def step(self, value):
        raise ValueError("step payload")


class FinalError(Sum):
    def finalize(self):
        raise ValueError("final payload")


class FinalAttributeError(Sum):
    def finalize(self):
        raise AttributeError("final attribute payload")


expect_error(sqlite3.OperationalError, "init_error", InitError,
             "user-defined aggregate's '__init__' method raised error")
expect_error(sqlite3.OperationalError, "missing_step", NoStep,
             "user-defined aggregate's 'step' method not defined")
expect_error(sqlite3.OperationalError, "missing_final", NoFinalize,
             "user-defined aggregate's 'finalize' method not defined")
expect_error(sqlite3.OperationalError, "step_error", StepError,
             "user-defined aggregate's 'step' method raised error")
expect_error(sqlite3.OperationalError, "final_error", FinalError,
             "user-defined aggregate's 'finalize' method raised error")
expect_error(sqlite3.OperationalError, "final_attr_error", FinalAttributeError,
             "user-defined aggregate's 'finalize' method not defined")
expect_error(sqlite3.OperationalError, "none_factory", None,
             "user-defined aggregate's '__init__' method raised error")


class OverflowStep(Sum):
    def step(self, value):
        raise OverflowError("callback overflow")


class MemoryStep(Sum):
    def step(self, value):
        raise MemoryError("callback memory")


expect_error(sqlite3.DataError, "overflow_step", OverflowStep)
expect_error(MemoryError, "memory_step", MemoryStep)
for value, expected in [(2**63, sqlite3.DataError), (object(), sqlite3.OperationalError),
                        ("\ud800", sqlite3.OperationalError),
                        (memoryview(b"abcdef")[::2], sqlite3.OperationalError)]:
    result_value = value
    try:
        con.execute("SELECT result_value()").fetchone()
    except expected:
        pass
    else:
        raise AssertionError("invalid callback result accepted")
    assert con.execute("SELECT 42").fetchone()[0] == 42
print("aggregate-errors-cleanup")


payload = bytearray(b"a")


class ResultLifetime:
    def step(self):
        pass

    def finalize(self):
        return payload

    def __del__(self):
        payload[0] = ord("b")


con.create_aggregate("result_lifetime", 0, ResultLifetime)
assert con.execute("SELECT result_lifetime()").fetchone()[0] == b"b"
lifetime_events = []


class Factory:
    def __call__(self):
        return Sum()

    def __del__(self):
        lifetime_events.append("factory released")


factory = Factory()
factory_ref = weakref.ref(factory)
con.create_aggregate("factory_sum", 1, factory)
del factory
assert factory_ref() is not None
assert con.execute("SELECT factory_sum(x) FROM input").fetchone()[0] == 11
con.create_aggregate("factory_sum", 1, Sum)
assert factory_ref() is None and lifetime_events == ["factory released"]
print("aggregate-lifetime-replacement")


with warnings.catch_warnings(record=True) as captured:
    warnings.simplefilter("always", DeprecationWarning)
    con.create_aggregate(name="keyword_sum", n_arg=1, aggregate_class=Sum)
assert len(captured) == 1 and captured[0].category is DeprecationWarning
assert con.execute("SELECT keyword_sum(x) FROM input").fetchone()[0] == 11
for arity, expected in [(-2, sqlite3.ProgrammingError), (2**100, OverflowError), (1.5, TypeError)]:
    try:
        con.create_aggregate("bad_arity", arity, Sum)
    except expected:
        pass
    else:
        raise AssertionError("invalid arity accepted")
try:
    con.create_aggregate("embedded\x00name", 1, Sum)
except ValueError:
    pass
else:
    raise AssertionError("NUL name accepted")
con.close()
try:
    con.create_aggregate("closed_sum", 1, Sum)
except sqlite3.ProgrammingError:
    pass
else:
    raise AssertionError("closed connection accepted")
print("aggregate-registration")
