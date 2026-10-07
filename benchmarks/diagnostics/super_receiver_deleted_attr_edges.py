import asyncio
import sys


class Base:
    def value(self):
        return self.tag

    @classmethod
    def kind(cls):
        return cls.__name__


class Child(Base):
    def value(receiver):
        extra = [1, 2, 3]
        return super().value(), extra

    def captured(receiver, replacement):
        def replace():
            nonlocal receiver
            receiver = replacement
        replace()
        return super().value()

    def reassigned(receiver, replacement):
        receiver = replacement
        return super().value()

    def deleted(receiver):
        del receiver
        return super().value()

    def deleted_cell(receiver):
        def remove():
            nonlocal receiver
            del receiver
        remove()
        return super().value()

    def deferred(receiver):
        yield 'start'
        yield super().value()

    async def async_value(receiver):
        await asyncio.sleep(0)
        return super().value()

    @classmethod
    def kind(receiver):
        return super().kind()


class Grandchild(Child):
    pass


first, second = Grandchild(), Grandchild()
first.tag, second.tag = 'first', 'second'
assert first.value() == ('first', [1, 2, 3])
assert first.reassigned(second) == 'second'
assert first.captured(second) == 'second'
assert Grandchild.kind() == 'Grandchild'
assert list(first.deferred()) == ['start', 'first']
assert asyncio.run(first.async_value()) == 'first'
print('live receiver, classmethod, cells and suspended frames', True)


class Alternate(Child):
    copied_value = Child.value


alternate = Alternate()
alternate.tag = 'alternate'
assert alternate.copied_value() == ('alternate', [1, 2, 3])
print('lexical class preserved for reassigned methods', True)

for function in (first.deleted, first.deleted_cell):
    try:
        function()
    except RuntimeError:
        pass
    else:
        raise AssertionError('deleted receiver revived')
print('deleted local and cell receivers rejected', True)

events = []


def trace(frame, event, arg):
    if frame.f_code.co_name == 'value' and event == 'call':
        events.append(event)
    return trace


sys.settrace(trace)
try:
    assert first.value() == ('first', [1, 2, 3])
finally:
    sys.settrace(None)
assert len(events) >= 2
print('traced Python forwarding executes original frames', True)
