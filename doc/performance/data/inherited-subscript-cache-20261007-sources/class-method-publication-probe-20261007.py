"""Reentrant descriptor finalizers must see an already replaced class method."""
import json

events = []


class Parent:
    pass


class Child(Parent):
    pass


child = Child()


def access(instance):
    return instance[1]


class PreviousDescriptor:
    def __get__(self, instance, owner):
        return lambda index: 'old'

    def __del__(self):
        events.append(access(child))


def install():
    Parent.__getitem__ = PreviousDescriptor()


install()
assert access(child) == 'old'
Parent.__getitem__ = lambda self, index: 'new'
print(json.dumps({'events': events, 'after': access(child)}))
