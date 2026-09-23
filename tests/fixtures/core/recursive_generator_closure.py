from itertools import groupby


def recursive(values):
    first = values[0]
    if len(values) == 1 or not first:
        return first
    return "|".join(
        recursive([item[1:] for item in group[1]])
        for group in groupby(values, lambda item: item[0] == first[0])
    )


print(recursive(("assert", "async", "await", "break")))
print(recursive(("aa", "ba")))
