import annotationlib


class Model:
    optional: int | None
    missing: MissingType


def convert(value: list[int]) -> str:
    return str(value)


print(annotationlib.get_annotations(Model, format=annotationlib.Format.STRING))
print(annotationlib.get_annotations(convert, format=annotationlib.Format.STRING))
forward = annotationlib.get_annotations(
    Model, format=annotationlib.Format.FORWARDREF
)["missing"]
print(type(forward).__name__, forward.__forward_arg__)
