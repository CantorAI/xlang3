import annotationlib
from typing import Annotated


def direct() -> Annotated[int, lambda value: f"number={value}"]:
    pass


def forward() -> Annotated[int, lambda value: f"number={value}"]:
    pass


annotations = getattr(direct, "__annotations__", None)
print("getattr", isinstance(annotations, dict), annotations["return"].__metadata__[0](3))
print("cached", annotations is direct.__annotations__)

first_forward = annotationlib.get_annotations(
    forward, format=annotationlib.Format.FORWARDREF
)["return"]
print("forward", first_forward.__metadata__[0](4))
