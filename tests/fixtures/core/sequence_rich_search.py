values = [{"a": 1}, {"a": 2}, {"a": 1}]
print(values.count({"a": 1}))
print(values.index({"a": 2}))
values.remove({"a": 1})
print(values)
print(({"a": 1}, {"a": 2}).count({"a": 1}))
print(({"a": 1}, {"a": 2}).index({"a": 2}))

try:
    set([{}])
except Exception as error:
    print(type(error).__name__, str(error))
