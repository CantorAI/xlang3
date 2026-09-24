def xor_table():
    return [bytes(a ^ b for a in range(4)) for b in range(4)]


print([list(row) for row in xor_table()])
print([list(row) for row in [bytes(a ^ b for a in range(3)) for b in range(3)]])


def deferred():
    return [(a + b for a in range(2)) for b in range(3)]


print([list(row) for row in deferred()])


def sized_table(size):
    return [list(bytes(a ^ b for a in range(size))) for b in range(size)]


print(sized_table(4))


async def async_values():
    for value in range(3):
        yield value


async def async_table():
    return [list(bytes(a ^ b for a in range(3))) async for b in async_values()]


import asyncio

print(asyncio.run(async_table()))
