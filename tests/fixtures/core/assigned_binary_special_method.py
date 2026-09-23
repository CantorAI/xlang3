import asyncio


class Addable:
    def __init__(self, value):
        self.value = value

    def __add__(self, other):
        return Addable(self.value + other.value)


def add_sync(left, right):
    result = left + right
    return result.value


async def add_async(left, right):
    result = left + right
    return result.value


print(add_sync(Addable(20), Addable(22)))
print(asyncio.run(add_async(Addable(19), Addable(23))))
