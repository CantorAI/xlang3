class Box:
    __match_args__ = ("value",)

    def __init__(self, value):
        self.value = value


def classify(value):
    match value:
        case Box(value=str() as text) as whole if text.startswith("x"):
            return (text, whole is value)
        case Box(value=[first, int() as second]):
            return (first, second)
        case _:
            return None


print(classify(Box("xlang")))
print(classify(Box([3, 4])))
print(classify(Box("other")))
