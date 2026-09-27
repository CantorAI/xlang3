first = float("nan")
second = float("nan")

print(first == first, first is first, first is second)
print({"input": first} == {"input": first})
print({"input": first} == {"input": second})
print([first] == [first], [first] == [second])
print((first,) == (first,), (first,) == (second,))
print(len({first: 1, second: 2}))
