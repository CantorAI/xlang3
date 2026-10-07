import functools
import sys


class Target:
    def __init__(self, offset):
        self.offset = offset

    def calculate(self, value=0):
        return self.offset + value


class Owner:
    def __init__(self, offset):
        self.target = Target(offset)

    @property
    def operation(self):
        return self.target.calculate

    @functools.cached_property
    def cached(self):
        return self.target.calculate


owner = Owner(10)
for value in range(3):
    assert owner.operation(value=value) == 10 + value
    assert owner.cached(value=value) == 10 + value
    assert owner.operation(value) == 10 + value
    assert owner.cached(value) == 10 + value
owner.target = Target(20)
assert owner.operation(value=2) == 22
assert owner.cached(value=2) == 12
owner.__dict__["cached"] = lambda value: value * 3
assert owner.cached(value=4) == 12


class ReadFailure:
    def __get__(self, instance, owner):
        raise LookupError("descriptor failure")


class BadOwner:
    operation = ReadFailure()


try:
    BadOwner().operation(value=1)
except LookupError as error:
    assert str(error) == "descriptor failure"
else:
    assert False, "descriptor error lost its original type"


events = []


def profile(frame, event, arg):
    if event == "c_call" and arg is getattr:
        events.append("getattr")


sys.setprofile(profile)
assert owner.operation(value=5) == 25
sys.setprofile(None)
assert not events, "implicit attribute lookup exposed an artificial getattr call"
print("callable descriptor keyword calls ok")
