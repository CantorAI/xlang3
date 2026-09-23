import copy


value = {'b'}
shallow = copy.copy(value)
deep = copy.deepcopy(value)

print(shallow == value, shallow is value)
print(deep == value, deep is value)
print(value.__reduce__())
print(value.__reduce_ex__(4))
