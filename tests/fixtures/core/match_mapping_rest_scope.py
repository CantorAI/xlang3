def extract(value):
    match value:
        case {"key": item, **rest}:
            return item, rest
    return None


def rest_only(value):
    match value:
        case {**rest}:
            return rest


def factory():
    class Keys:
        item = "key"

    def closed(value):
        match value:
            case {Keys.item: item, **rest}:
                return item, rest
        return None

    return closed


print("key-and-rest", extract({"key": 7, "extra": 9}))
print("rest-only", rest_only({"key": 7}))
print("empty-rest", extract({"key": 7}))
print("miss", extract({"other": 7}))
closed = factory()
print("closure", closed({"key": 11, "extra": 13}))
print("closure-miss", closed({"other": 11}))
