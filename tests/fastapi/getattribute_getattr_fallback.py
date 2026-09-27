class Dynamic:
    def __getattribute__(self, name):
        return super().__getattribute__(name)

    def __getattr__(self, name):
        if name == "calculate":
            return lambda: 42
        raise AttributeError(name)


value = Dynamic()
print(value.calculate())
print(getattr(value, "calculate")())
print(hasattr(value, "missing"))
