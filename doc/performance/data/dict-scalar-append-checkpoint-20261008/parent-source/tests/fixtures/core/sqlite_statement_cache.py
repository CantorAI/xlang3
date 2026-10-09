"""Prepared SQL reuse preserves native cursor, parameter and callback semantics."""
import gc
import operator
import sqlite3
import weakref


class CacheSize:
    def __index__(self):
        return 2


for size in (0, -1, 1, CacheSize()):
    con = sqlite3.connect(':memory:', cached_statements=size)
    con.execute('CREATE TABLE values_seen(x)')
    for value in (17, None, 'text\x00payload', b'\x00\xff', ''):
        cur = con.execute('INSERT INTO values_seen VALUES (?)', (value,))
        assert cur.rowcount == 1 and cur.lastrowid is not None
    assert [row[0] for row in con.execute('SELECT x FROM values_seen ORDER BY rowid')] == [
        17, None, 'text\x00payload', b'\x00\xff', '']
    con.close()
for bad in (1.5, '2'):
    try:
        sqlite3.connect(':memory:', cached_statements=bad)
    except TypeError:
        pass
    else:
        raise AssertionError('cache capacity must use the integer index protocol')
direct = sqlite3.Connection(':memory:', cached_statements=0)
direct.close()
print('configurable cache and fresh parameter values')

index_calls = []


class IntrinsicSize:
    def __index__(self):
        index_calls.append('class')
        return 2

    def __getattribute__(self, name):
        if name == '__index__':
            return lambda: 0
        return object.__getattribute__(self, name)


capacity = IntrinsicSize()
capacity.__index__ = lambda: 0
saved_index = operator.index
operator.index = lambda value: (_ for _ in ()).throw(AssertionError('public operator.index called'))
try:
    intrinsic = sqlite3.connect(':memory:', cached_statements=capacity)
    intrinsic.execute('SELECT 1').fetchone()
    intrinsic.close()
finally:
    operator.index = saved_index
assert index_calls == ['class']
marker = LookupError('index failure identity')


class FailingSize:
    def __index__(self):
        raise marker


try:
    sqlite3.connect(':memory:', cached_statements=FailingSize())
except LookupError as caught:
    assert caught is marker
else:
    raise AssertionError('index callback exception was replaced')


class OverflowSize:
    def __index__(self):
        return 2 ** 40


for value in (2 ** 40, OverflowSize()):
    try:
        sqlite3.connect(':memory:', cached_statements=value)
    except OverflowError:
        pass
    else:
        raise AssertionError('cache capacity must respect the native C-int range')
print('intrinsic index ignores public and instance overrides and preserves failures')

con = sqlite3.connect(':memory:', cached_statements=2)
sql = 'SELECT ? AS x UNION ALL SELECT ?'
first = con.execute(sql, (1, 2))
second = con.execute(sql, (3, 4))
assert tuple(first.fetchone()) == (1,) and tuple(second.fetchone()) == (3,)
assert tuple(first.fetchone()) == (2,) and tuple(second.fetchone()) == (4,)
assert first.fetchone() is None and second.fetchone() is None
assert first.description[0][0] == second.description[0][0] == 'x'
assert tuple(con.execute(sql, (5, 6)).fetchone()) == (5,)
print('overlapping identical statements have independent cursor leases')

con.execute('CREATE TABLE nested_values(x)')
insert = 'INSERT INTO nested_values VALUES (?)'
con.execute(insert, (0,))


class Parameters:
    def __len__(self):
        return 1

    def __getitem__(self, index):
        assert index == 0
        con.execute(insert, (2,))
        return 1


con.execute(insert, Parameters())
assert [row[0] for row in con.execute('SELECT x FROM nested_values ORDER BY rowid')] == [0, 2, 1]
try:
    con.execute(insert, (object(),))
except sqlite3.Error:
    pass
else:
    raise AssertionError('unsupported binding did not fail')
con.execute(insert, (9,))
assert [row[0] for row in con.execute('SELECT x FROM nested_values ORDER BY rowid')] == [0, 2, 1, 9]
print('binding callbacks and failures leave reusable statements clean')

con.execute('CREATE TABLE schema_values(x)')
con.execute('INSERT INTO schema_values VALUES (?)', (1,))
assert tuple(con.execute('SELECT x FROM schema_values').fetchone()) == (1,)
con.execute('ALTER TABLE schema_values ADD COLUMN y')
con.execute('INSERT INTO schema_values(x) VALUES (?)', (2,))
assert [tuple(row) for row in con.execute('SELECT x FROM schema_values')] == [(1,), (2,)]
returning = con.execute('INSERT INTO schema_values(x) VALUES (3), (4) RETURNING x')
assert returning.rowcount == 0 and tuple(returning.fetchone()) == (3,)
assert tuple(returning.fetchone()) == (4,) and returning.rowcount == 2
assert returning.fetchone() is None and returning.description[0][0] == 'x'
print('schema reprepare and returning progression preserve metadata')


class Sum:
    def __init__(self):
        self.total = 0

    def step(self, value):
        self.total += value

    def finalize(self):
        return self.total


events = []


class Factory:
    def __call__(self):
        return Sum()

    def __del__(self):
        events.append('released')


factory = Factory()
reference = weakref.ref(factory)
con.create_aggregate('cached_sum', 1, factory)
del factory
aggregate_sql = 'SELECT cached_sum(x) FROM (SELECT 1 AS x UNION ALL SELECT 2)'
retained = con.execute(aggregate_sql)
assert tuple(retained.fetchone()) == (3,)
con.create_aggregate('cached_sum', 1, Sum)
gc.collect()
assert reference() is None and events == ['released']
assert retained.description[0][0] == 'cached_sum(x)'
assert tuple(con.execute(aggregate_sql).fetchone()) == (3,)
print('cached completion preserves aggregate replacement and factory lifetime')

con.create_function('cached_scalar', 0, lambda: 7)
assert tuple(con.execute('SELECT cached_scalar()').fetchone()) == (7,)
con.create_function('cached_scalar', 0, lambda: 8)
assert tuple(con.execute('SELECT cached_scalar()').fetchone()) == (8,)
active = con.execute('SELECT 1 UNION ALL SELECT 2')
con.close()
try:
    active.fetchone()
except sqlite3.ProgrammingError:
    pass
else:
    raise AssertionError('closed connection must reject active cursor use')
del active
gc.collect()
con.close()
print('scalar replacement and active close keep native ownership safe')

