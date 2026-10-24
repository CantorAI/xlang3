"""Dict special lookup must be type based and avoid storage-method callbacks."""
from collections import Counter
import sys
import traceback


class Missing(dict):
    def __missing__(self, key):
        return ('type', key)


mapping = Missing()
mapping.__missing__ = lambda key: ('instance', key)
for key in ('absent', (b'left', b'right')):
    assert mapping[key] == ('type', key)
    assert dict.__getitem__(mapping, key) == ('type', key)
assert Counter()['absent'] == 0
print('subscript and explicit dict getitem ignore instance missing attributes: OK')


class Hooked(Missing):
    def __getattribute__(self, name):
        if name == '__missing__':
            raise AssertionError('ordinary attribute lookup used')
        return object.__getattribute__(self, name)


assert Hooked()['absent'] == ('type', 'absent')
Missing.__missing__ = lambda self, key: ('replacement', key)
assert mapping['absent'] == ('replacement', 'absent')
del Missing.__missing__
try:
    mapping['absent']
except KeyError:
    pass
else:
    raise AssertionError('deleted class method still called')
print('type lookup ignores getattribute hooks and observes class mutation: OK')


class Descriptor:
    def __get__(self, instance, owner):
        assert owner is Custom and isinstance(instance, Custom)
        return lambda key: ('descriptor', key)


class Custom(dict):
    __missing__ = Descriptor()


class Static(dict):
    @staticmethod
    def __missing__(key):
        return key


class Class(dict):
    @classmethod
    def __missing__(cls, key):
        return cls


class Property(dict):
    @property
    def __missing__(self):
        return lambda key: ('property', key)


for key in ('absent', (b'left', b'right')):
    assert Custom()[key] == ('descriptor', key)
    assert dict.__getitem__(Custom(), key) == ('descriptor', key)
    assert Static()[key] == key
    assert Class()[key] is Class
    assert Property()[key] == ('property', key)
print('user descriptors static class and property missing hooks bind correctly: OK')


class BrokenDescriptor:
    def __get__(self, instance, owner):
        raise LookupError('binding failure')


class Broken(dict):
    __missing__ = BrokenDescriptor()


for key in ('absent', (b'left', b'right')):
    for operation in (lambda: Broken()[key], lambda: dict.__getitem__(Broken(), key)):
        try:
            operation()
        except LookupError as error:
            assert type(error) is LookupError and str(error) == 'binding failure'
            assert traceback.extract_tb(error.__traceback__)[-1].name == '__get__'
        else:
            raise AssertionError('descriptor failure swallowed')
print('descriptor exceptions retain their class message and Python traceback: OK')


class Storage(dict):
    def __missing__(self, key):
        raise AssertionError('storage called missing')

    def __getitem__(self, key):
        raise AssertionError('storage called getitem')

    def __setitem__(self, key, value):
        raise AssertionError('storage called setitem')


for key in ('absent', (b'left', b'right')):
    storage = Storage()
    assert key not in storage
    assert storage.get(key, 7) == 7
    assert storage.pop(key, 7) == 7
    assert storage.setdefault(key, 9) == 9
    assert storage.setdefault(key, 11) == 9
    assert key in storage
    assert dict.__getitem__(storage, key) == 9
    assert storage.pop(key) == 9
print('membership get pop and setdefault use storage without subclass hooks: OK')


class Traced(dict):
    def __missing__(self, key):
        return 0


events = []


def hook(frame, event, arg):
    if frame.f_code.co_name == '__missing__' and event in ('call', 'return'):
        events.append(event)
    return hook


sys.settrace(hook)
try:
    assert Traced()['absent'] == 0
finally:
    sys.settrace(None)
assert events == ['call', 'return'], events
print('direct raw method dispatch retains Python tracing: OK')
