def pair(first, second):
    return first, second


def pair_with_keyword_only(first, *, second=99):
    return first, second


print(pair("a", second="b"))
print(pair_with_keyword_only("x", second="y"))

try:
    pair("a", first="b")
except TypeError as error:
    print(type(error).__name__, str(error))

try:
    pair("a", other="b")
except TypeError as error:
    print(type(error).__name__, str(error))

try:
    pair("a", second="b", **{})
except TypeError as error:
    print(type(error).__name__, str(error))
