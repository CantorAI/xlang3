class Text(str):
    pass


class SlottedText(str):
    __slots__ = ("tag",)

    def __new__(cls, value, tag):
        self = super().__new__(cls, value)
        self.tag = tag
        return self


left = Text("left")
right = Text("right")
print(left + "-plain", type(left + "-plain").__name__)
print("plain-" + right, type("plain-" + right).__name__)
print(left + Text("-subclass"), type(left + Text("-subclass")).__name__)
slotted = SlottedText("slot", 7)
print(str(slotted), slotted + "-value", slotted.tag, len(slotted))
try:
    str.__add__(left, 1)
except Exception as exc:
    print(type(exc).__name__)
