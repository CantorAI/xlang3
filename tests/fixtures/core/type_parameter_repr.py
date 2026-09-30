from typing import ParamSpec, TypeVar, TypeVarTuple


for parameter in (
    TypeVar('T'),
    TypeVar('Co', covariant=True),
    TypeVar('Contra', contravariant=True),
    ParamSpec('P'),
    ParamSpec('Q', covariant=True),
    TypeVarTuple('Ts'),
):
    print(repr(parameter), str(parameter))
