class Label(str):
    pass


class Values:
    def __getitem__(self, key):
        return {"name": "mapped"}[key]


mapped = Label("value=%(name)s") % Values()
positional = Label("%s:%d") % ("item", 7)
print(mapped, type(mapped).__name__)
print(positional, type(positional).__name__)
