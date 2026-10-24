from collections import Counter
import traceback


def read(owner, key):
    return owner[key]


def write(owner, key, value):
    owner[key] = value


class NativeParent(dict):
    __getitem__ = dict.__getitem__
    __setitem__ = dict.__setitem__


class NativeChild(NativeParent):
    pass


native = NativeChild()
for value in range(5):
    write(native, (b'key',), value)
    assert read(native, (b'key',)) == value
counter = Counter()
assert read(counter, (b'missing',)) == 0
write(counter, (b'present',), 9)
assert read(counter, (b'present',)) == 9
try:
    read(native, (b'absent',))
except KeyError:
    pass
else:
    raise AssertionError('native missing key accepted')
print('inherited native methods and Counter missing keys: OK')


def changed_getitem(self, key):
    return ('changed', key)


NativeParent.__getitem__ = changed_getitem
assert read(native, 2) == ('changed', 2)
NativeParent.__getitem__ = dict.__getitem__
assert read(native, (b'key',)) == 4
print('native base replacement invalidates inherited call sites: OK')


class Grandparent:
    def __getitem__(self, key):
        return ('grandparent', key)


class Parent(Grandparent):
    def __getitem__(self, key):
        return ('parent', key)

    def __setitem__(self, key, value):
        self.last = ('parent', key, value)


class Child(Parent):
    pass


child = Child()
assert read(child, 1) == ('parent', 1)
write(child, 2, 3)
assert child.last == ('parent', 2, 3)
Parent.__getitem__ = changed_getitem
assert read(child, 4) == ('changed', 4)
del Parent.__getitem__
assert read(child, 5) == ('grandparent', 5)


def changed_setitem(self, key, value):
    self.last = ('changed', key, value)


Parent.__setitem__ = changed_setitem
write(child, 6, 7)
assert child.last == ('changed', 6, 7)
print('inherited Python methods, base replacement and deletion: OK')


class OtherBase:
    def __getitem__(self, key):
        return ('other', key)


Parent.__bases__ = (OtherBase,)
assert read(child, 8) == ('other', 8)
print('base reassignment invalidates descendant MRO caches: OK')


class Descriptor:
    def __get__(self, instance, owner):
        return lambda key: ('descriptor', key)


Parent.__getitem__ = Descriptor()
assert read(child, 9) == ('descriptor', 9)
Parent.__getitem__ = staticmethod(lambda key: ('static', key))
assert read(child, 10) == ('static', 10)
Parent.__getitem__ = classmethod(lambda cls, key: (cls.__name__, key))
assert read(child, 11) == ('Child', 11)
print('descriptors, staticmethod and classmethod keep normal binding: OK')

events = []


class PreviousDescriptor:
    def __get__(self, instance, owner):
        return lambda key: 'old'

    def __del__(self):
        events.append(read(child, 12))


def install_previous():
    Parent.__getitem__ = PreviousDescriptor()


install_previous()
assert read(child, 12) == 'old'
Parent.__getitem__ = lambda self, key: 'new'
assert events == ['new'], events
assert read(child, 12) == 'new'
print('reentrant finalizer sees published replacement and invalidated caches: OK')


class RaisingParent:
    def __getitem__(self, key):
        raise LookupError('read failed')

    def __setitem__(self, key, value):
        raise LookupError('write failed')


class RaisingChild(RaisingParent):
    pass


raising = RaisingChild()
for operation, expected_name in ((lambda: read(raising, 1), '__getitem__'),
                                 (lambda: write(raising, 1, 2), '__setitem__')):
    try:
        operation()
    except LookupError as error:
        frames = traceback.extract_tb(error.__traceback__)
        assert frames[-1].name == expected_name
    else:
        raise AssertionError('inherited method error lost')
print('inherited Python errors retain their method traceback frame: OK')
