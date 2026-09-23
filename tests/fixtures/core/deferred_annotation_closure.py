import annotationlib


def make_function():
    class LocalType:
        pass

    def annotated(value: LocalType) -> tuple[LocalType, ...]:
        return (value,)

    return annotated, LocalType


annotated, local_type = make_function()
annotations = annotated.__annotations__
print(annotations["value"] is local_type)
print(annotations["return"] == tuple[local_type, ...])
print(annotationlib.get_annotations(annotated, format=annotationlib.Format.STRING))


def make_annotated_class():
    class LocalType:
        pass

    class Record:
        value: LocalType

    return Record, LocalType


record, local_type = make_annotated_class()
print(record.__annotations__["value"] is local_type)
