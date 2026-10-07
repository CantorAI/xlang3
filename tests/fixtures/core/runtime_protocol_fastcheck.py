import sys
from typing import Protocol, runtime_checkable


@runtime_checkable
class HasValue(Protocol):
    value: int


@runtime_checkable
class HasRun(Protocol):
    def run(self): ...


@runtime_checkable
class HasProperty(Protocol):
    @property
    def item(self): ...


class ValuePresent:
    value = None


class ValueMissing:
    pass


class RunMissing:
    pass


class RunNone:
    run = None


class PropertyPresent:
    @property
    def item(self):
        raise AssertionError("runtime protocol checks must not invoke descriptors")


class CustomGetattribute:
    value = 42

    def __getattribute__(self, name):
        if name == "__class__":
            return type(self)  # The native ABC check legitimately reads this.
        raise AssertionError("runtime protocol checks must use static lookup")


class ImplementsRun(HasRun):
    def run(self):
        return 1


print(isinstance(ValuePresent(), HasValue))
print(isinstance(ValueMissing(), HasValue))
print(isinstance(RunMissing(), HasRun))
print(isinstance(RunNone(), HasRun))
print(isinstance(PropertyPresent(), HasProperty))
print(isinstance(CustomGetattribute(), HasValue))
print(isinstance(ImplementsRun(), HasRun))

class NotRuntimeCheckable(Protocol):
    def method(self): ...


try:
    isinstance(RunMissing(), NotRuntimeCheckable)
except TypeError as exc:
    print("non-runtime protocol error", "@runtime_checkable" in str(exc))


protocol_meta = type(HasRun)
original_instancecheck = protocol_meta.__instancecheck__


def custom_instancecheck(cls, instance):
    return instance is None


protocol_meta.__instancecheck__ = custom_instancecheck
print("custom instancecheck", isinstance(None, HasRun))
protocol_meta.__instancecheck__ = original_instancecheck

# The library hook must remain Python code, including during profiling.
# A native replacement of its member loop used to hide this call entirely.
protocol_calls = []
protocol_filename = original_instancecheck.__code__.co_filename


def record_protocol_calls(frame, event, arg):
    if (event == "call" and frame.f_code.co_name == "__instancecheck__"
            and frame.f_code.co_filename == protocol_filename):
        protocol_calls.append(frame.f_code.co_name)


subject = ValuePresent()
sys.setprofile(record_protocol_calls)
try:
    profile_result = isinstance(subject, HasValue)
finally:
    sys.setprofile(None)
print("python protocol hook", profile_result, protocol_calls == ["__instancecheck__"])
