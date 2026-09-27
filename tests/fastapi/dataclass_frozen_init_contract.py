from pydantic.dataclasses import dataclass


@dataclass(frozen=True)
class Frozen:
    value: int


item = Frozen('7')
print('initialized', item.value)
try:
    item.value = 8
except AttributeError as error:
    print('frozen', type(error).__name__)
