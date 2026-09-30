class Example:
    pass


Example.__name__ = "Renamed"
print(Example.__name__, Example.__qualname__)
print(Example.__dict__.get("__name__"))
try:
    Example.__name__ = 123
except TypeError:
    print("TypeError")
print(Example.__name__)
