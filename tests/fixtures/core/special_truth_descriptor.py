from unittest.mock import MagicMock


class TruthDescriptor:
    def __get__(self, instance, owner):
        return lambda: False


class LengthDescriptor:
    def __get__(self, instance, owner):
        return lambda: 3


class FalseByDescriptor:
    __bool__ = TruthDescriptor()


class LengthByDescriptor:
    __len__ = LengthDescriptor()


print(bool(FalseByDescriptor()))
print(bool(LengthByDescriptor()))
print(bool(MagicMock()))
mask = MagicMock() | MagicMock()
print(type(mask).__name__, mask.__index__())
print(type(MagicMock()[1]).__name__)
