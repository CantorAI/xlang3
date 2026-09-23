from enum import Enum


class ByteChild(bytes):
    pass


class ByteEnum(bytes, Enum):
    item = b"enum-value"


print(ByteChild(b"child").decode("ascii"))
print(ByteEnum.item.decode("ascii"))


def failing_values():
    yield "first"
    raise ValueError("join source failed")


try:
    "-".join(failing_values())
except ValueError as exc:
    print(type(exc).__name__, str(exc))

try:
    b"\x81".decode("utf-8")
except UnicodeDecodeError as exc:
    print(type(exc).__name__, str(exc))
