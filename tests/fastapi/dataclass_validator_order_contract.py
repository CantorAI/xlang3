from pydantic import field_validator
from pydantic.dataclasses import dataclass


def check():
    @dataclass
    class Parent:
        values: list[str]

        @field_validator('values')
        @classmethod
        def before(cls, value):
            value.append('parent before')
            return value

        @field_validator('values')
        @classmethod
        def val(cls, value):
            value.append('parent')
            return value

        @field_validator('values')
        @classmethod
        def after(cls, value):
            value.append('parent after')
            return value

    @dataclass
    class Child(Parent):
        @field_validator('values')
        @classmethod
        def val(cls, value):
            value.append('child')
            return value

        @field_validator('values')
        @classmethod
        def child_after(cls, value):
            value.append('child after')
            return value

    print(Parent(values=[]).values)
    print(Child(values=[]).values)


check()
