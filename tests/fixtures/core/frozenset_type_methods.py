left = frozenset({1, 2})
right = frozenset.union(left, {2, 3})

print(type(left.copy()).__name__)
print(type(right).__name__, sorted(right))
print(type(left.intersection({2, 4})).__name__)
print(type(left.difference({2})).__name__)
print(type(left.symmetric_difference({2, 3})).__name__)
print(hasattr(frozenset, "union"), hasattr(frozenset, "add"))
