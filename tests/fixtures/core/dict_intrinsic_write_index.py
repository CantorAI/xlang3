from collections import Counter


keys = [(bytes([index]), b'right') for index in range(128)]
for mapping in ({}, Counter()):
    for key in keys:
        mapping[key] = 0
    for unused in range(3):
        for key in keys:
            mapping[(key[0], key[1])] += 1
    assert len(mapping) == 128 and list(mapping) == keys
    assert all(mapping[key] == 3 for key in keys)
print('plain dict and Python Counter updates: OK')

original = tuple([b'key', 1])
equal = tuple([b'key', True])
mapping = {original: 'first'}
mapping[equal] = 'updated'
assert len(mapping) == 1 and next(iter(mapping)) is original
assert mapping[original] == mapping[equal] == 'updated'
assert hash((-1,)) == hash((-2,))
mapping[(-1,)] = 'minus one'
mapping[(-2,)] = 'minus two'
assert mapping[(-1,)] == 'minus one' and mapping[(-2,)] == 'minus two'
nested = ((b'child', 123), None, 1 << 100)
mapping[nested] = 'nested'
assert mapping[((b'child', 123), None, 1 << 100)] == 'nested'
print('equal distinct keys, collisions and nested bigint keys: OK')

mapping['text'] = 'string'
mapping[12] = 'integer'
mapping[(b'new',)] = 'tuple'
assert mapping['text'] == 'string' and mapping[12] == 'integer'
assert mapping[(b'new',)] == 'tuple'
del mapping[equal]
assert original not in mapping
mapping[equal] = 'reinserted'
assert list(mapping)[-1] is equal
last, value = mapping.popitem()
assert last is equal and value == 'reinserted'
mapping[(b'after_pop',)] = 7
assert mapping[(b'after_pop',)] == 7
alias = mapping
mapping.clear()
mapping[b'reused'] = 9
assert alias is mapping and list(alias.items()) == [(b'reused', 9)]
print('mixed strings/integers, deletion, pop, clear and aliases: OK')


class Identity:
    pass


identity = Identity()
mapping[identity] = 'object'
mapping[(b'fallback',)] = 10
assert mapping[identity] == 'object' and mapping[(b'fallback',)] == 10
del mapping[identity]
mapping[(b'eligible_again',)] = 11
assert mapping[(b'eligible_again',)] == 11
try:
    mapping[([],)] = 1
except TypeError:
    pass
else:
    raise AssertionError('unhashable tuple accepted')
print('mixed object keys, eligibility recovery and unhashable tuples: OK')

class Missing(dict):
    def __missing__(self, key):
        return ('missing', key)


class Override(dict):
    def __getitem__(self, key):
        return ('override', key)


for cls in (Missing, Override):
    owner = cls()
    owner[(b'present',)] = 3
    assert owner[(b'absent',)] == (
        'missing' if cls is Missing else 'override', (b'absent',))
    assert owner[(b'present',)] == (
        3 if cls is Missing else ('override', (b'present',)))
assert Counter({(b'present',): 3})[(b'absent',)] == 0

# A custom query against intrinsic stored keys and intrinsic queries against a
# mixed dictionary must still execute Python hash/equality callbacks.
calls = []


class CallbackKey:
    def __hash__(self):
        calls.append('hash')
        return hash(b'target')

    def __eq__(self, other):
        calls.append('equal')
        return other == b'target'


callback = CallbackKey()
mapping = {b'target': 4, (b'indexed',): 5}
assert mapping[callback] == 4
assert 'hash' in calls and 'equal' in calls
mapping = {callback: 6, (b'indexed',): 7}
calls.clear()
assert mapping[b'target'] == 6 and 'equal' in calls

events = []


class Marker:
    def __init__(self, owner):
        self.owner = owner

    def __del__(self):
        events.append((list(self.owner), self.owner.get(b'live')))
        self.owner[b'callback'] = 7


def install(owner):
    owner[b'dead'] = Marker(owner)


mapping = {b'live': 9}
install(mapping)
del mapping[b'dead']
assert events == [([b'live'], 9)]
assert mapping[b'callback'] == 7
events.clear()
mapping = {}
install(mapping)
mapping.clear()
assert events == [([], None)]
assert list(mapping.items()) == [(b'callback', 7)]
print('removed-value finalizers see coherent indices and preserve new writes: OK')

events.clear()


class Previous:
    def __init__(self, owner):
        self.owner = owner

    def __del__(self):
        events.append(self.owner[(b'replace',)])
        self.owner.clear()
        self.owner[(b'callback',)] = 7


def install_previous(owner):
    owner[(b'replace',)] = Previous(owner)


owner = {}
install_previous(owner)
owner[(b'replace',)] = 9
assert events == [9], events
assert list(owner.items()) == [((b'callback',), 7)]
print('replacement published before finalizer; callback writes preserved: OK')
