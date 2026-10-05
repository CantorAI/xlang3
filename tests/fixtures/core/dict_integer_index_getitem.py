dispatch = {0: "zero", 1: "one"}
print(dispatch[0], dispatch[1])
print(1 in dispatch, 42 in dispatch)

# bool and int keys compare equal and share the same integer lookup index.
print({True: "bool"}[1])
print(1 in {True: "bool"})
print(3 in {3.0: "float"})

large = {}
for i in range(80):
    large[i] = i + 1
print(large[0], large[47], large[79], 80 in large)
del large[47]
print(47 in large)
large[47] = 500
large["marker"] = 0
print(large[47], 47 in large, 79.0 in large)


class EqualThree:
    def __hash__(self):
        return hash(3)

    def __eq__(self, other):
        return isinstance(other, int) and other == 3


# The integer-index fast path must miss back to runtime equality for this key.
print({EqualThree(): "custom"}[3])
