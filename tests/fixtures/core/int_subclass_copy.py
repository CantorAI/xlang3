import copy


class Number(int):
    pass


value = Number(7)
value.label = "seven"
copied = copy.deepcopy(value)
print(int(copied), type(copied).__name__, copied.label, copied is value)
print(value.__reduce_ex__(4)[1][1])
print(value / 2, 14 / value)
