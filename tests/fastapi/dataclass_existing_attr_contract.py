import dataclasses

from pydantic.dataclasses import dataclass


class Base:
    value: str
    calls = 0

    def __new__(cls, *args, **kwargs):
        cls.calls += 1
        instance = super().__new__(cls)
        instance.extra = 42
        return instance


Standard = dataclasses.dataclass(Base)
Validated = dataclass(Base)
standard = Standard('a')
validated = Validated('b')
print('new-called', Standard.calls, Validated.calls)
print('standard-extra', hasattr(standard, 'extra'))
print('validated-extra', hasattr(validated, 'extra'))
print('validated-value', validated.value)
