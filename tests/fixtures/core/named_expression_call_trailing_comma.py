def capture(*values):
    return values


result = capture(
    assigned := "value",
)
print(result, assigned)

nested = capture(
    first := (1, 2),
    second := "two",
)
print(nested, first, second)

tuple_rhs = (pair := (3, 4))
print(tuple_rhs, pair)


def make_reader(metadata):
    if captured := metadata.get("value"):
        def reader():
            return captured

        return reader
    return lambda: None


print(make_reader({"value": "closure"})())
