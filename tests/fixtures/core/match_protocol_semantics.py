from collections.abc import Mapping, Sequence


class RegisteredSequence:
    def __len__(self):
        return 2

    def __getitem__(self, index):
        return (4, 5)[index]


class RegisteredMapping:
    def __len__(self):
        return 1

    def __iter__(self):
        return iter(("key",))

    def __getitem__(self, key):
        return 7

    def get(self, key, default=None):
        return 7 if key == "key" else default

    def keys(self):
        return ("key",)


class MissingGetMapping(RegisteredMapping):
    get = None


Sequence.register(RegisteredSequence)
Mapping.register(RegisteredMapping)
Mapping.register(MissingGetMapping)


def classify(value):
    match value:
        case 1 | 2:
            return "number"
        case [first, *rest]:
            return "sequence", first, rest
        case {"key": item, **rest}:
            return "mapping", item, rest
        case None:
            return "none"
        case _:
            return "other"


for value in (1, 2, [3, 4], (6, 7), "hi", b"hi", {"key": 8, "extra": 9},
              RegisteredSequence(), RegisteredMapping(), None, object()):
    print(classify(value))

try:
    classify(MissingGetMapping())
except Exception as exc:
    print(type(exc).__name__)
