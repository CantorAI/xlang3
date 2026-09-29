def leaf():
    first = yield "first"
    second = yield ("sent", first)
    return ("return", second)


def relay():
    result = yield from leaf()
    yield result


generator = relay()
print(next(generator))
print(generator.send("alpha"))
print(generator.send("omega"))
try:
    next(generator)
except StopIteration:
    print("finished")


def many_values():
    for value in range(200):
        yield value


def delegated_values():
    yield from many_values()


print(sum(delegated_values()))
