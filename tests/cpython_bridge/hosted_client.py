def callback(value, offset=0):
    return value + offset

class Dynamic:
    def __getattribute__(self, name):
        if name == "answer":
            return lambda: 42
        raise AttributeError(name)

def dynamic_test():
    value = Dynamic()
    assert value.answer() == 42
    assert getattr(value, "answer")() == 42
    assert getattr(value, "missing", 17) == 17
    return True
