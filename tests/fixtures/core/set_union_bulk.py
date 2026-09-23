left = {1, 2}
right = {True, 2, 3}
merged = left | right
print(type(merged).__name__, len(merged), sorted(merged))
print(len(left), len(right))

frozen = frozenset((1, 2)) | {2, 3}
print(type(frozen).__name__, sorted(frozen))

large = set(range(5000)) | set(range(2500, 7500))
print(len(large), sum(large))
