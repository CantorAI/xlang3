
# These duplicate declarations deliberately use only their visible descriptor
# storage. The CPP eligibility checks assert generic fallback; this differential
# fixture does not claim independent Base.x/Child.x duplicate-layout support.
slot_history = ['x']
HistoryBase = type('HistoryBase', (), {'__slots__': slot_history})
HistoryChild = type('HistoryChild', (HistoryBase,), {'__slots__': ()})
history = HistoryChild()
history.x = first
repeated_x(history, first)
slot_history[:] = []
del HistoryBase.__slots__
repeated_x(history, first)
history.x = second
repeated_x(history, second)
print('original slot declaration list mutation and deletion preserve live storage: OK')

HiddenDuplicate = type('HiddenDuplicate', (HistoryBase,), {'__slots__': ('x',)})
hidden_duplicate = HiddenDuplicate()
hidden_duplicate.x = first
repeated_x(hidden_duplicate, first)
hidden_duplicate.x = second
repeated_x(hidden_duplicate, second)
RepeatedOwn = type('RepeatedOwn', (), {'__slots__': ('x', 'x')})
RepeatedChild = type('RepeatedChild', (RepeatedOwn,), {'__slots__': ()})
repeated_child = RepeatedChild()
repeated_child.x = first
repeated_x(repeated_child, first)
del repeated_child.x
assert getattr(repeated_child, 'x', second) is second
repeated_child.x = second
repeated_x(repeated_child, second)
print('duplicate declarations retain visible-descriptor writes and missing fallback: OK')

class EmptyIntermediate(HistoryBase):
    __slots__ = ()

class Added(EmptyIntermediate):
    __slots__ = ('extra', '__dict__')

added = Added()
added.x = first
added.extra = second
added.__dict__['x'] = second
repeated_x(added, first)
del added.__dict__['x']
del added.x
assert getattr(added, 'x', replacement) is replacement
added.x = second
repeated_x(added, second)
print('empty intermediate and added dict layouts retain inherited descriptor precedence: OK')

class PrivateBase:
    __slots__ = ('__x',)

class PrivateChild(PrivateBase):
    __slots__ = ()

private = PrivateChild()
private._PrivateBase__x = first
for unused in range(16):
    assert private._PrivateBase__x is first
print('private slot mangling retains inherited storage identity: OK')
