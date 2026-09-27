class Child(bytes):
    def __new__(cls, data):
        return bytes.__new__(cls, data)


value = Child(b'foobar')
print(type(value).__name__, len(value), value[0], repr(value),
      value == b'foobar', isinstance(value, bytes))
