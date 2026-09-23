class Metadata:
    def __init__(self, values):
        self.before = "discarded"
        self.__dict__ = values


values = {"pattern": "^[a-z]+$", "limit": 4}
metadata = Metadata(values)
print(metadata.pattern, metadata.limit, hasattr(metadata, "before"))
print(metadata.__dict__ is values)

values["label"] = "shared"
print(metadata.label)

replacement = {"answer": 42}
metadata.__dict__ = replacement
print(metadata.answer, hasattr(metadata, "pattern"), metadata.__dict__ is replacement)

try:
    metadata.__dict__ = []
except Exception as exc:
    print(type(exc).__name__)


class SlotOnly:
    __slots__ = ()


try:
    SlotOnly().__dict__ = {}
except Exception as exc:
    print(type(exc).__name__)
