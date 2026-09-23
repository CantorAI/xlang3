seen = []


def first(value):
    seen.append(("first", value))
    return value % 2 == 0


def second(value):
    seen.append(("second", value))
    return value > 1


print([value for value in range(5) if first(value) if second(value)])
print(seen)
print(tuple(value for value in range(5) if value > 0 if value < 4))
print({value for value in range(5) if value > 0 if value < 4})
print({value: value * 2 for value in range(5) if value > 0 if value < 4})


def generator_in_assert():
    for classifier in (lambda value: value > 0,):
        assert all(classifier(value) for value in (1, 2))
    return True


print(generator_in_assert())
