class CustomSet(set):
    pass


values = CustomSet()
values.add(3)
values.add(1)
print(issubclass(CustomSet, set), hasattr(CustomSet, "__iter__"))
print(len(values), sorted(values), 3 in values)
