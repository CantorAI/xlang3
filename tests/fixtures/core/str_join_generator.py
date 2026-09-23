values = ("alpha", 7, None)

print(", ".join(repr(value) for value in values))
print("-".join(str(value * 2) for value in range(4)))


def words():
    yield "north"
    yield "star"


print(" ".join(words()))
