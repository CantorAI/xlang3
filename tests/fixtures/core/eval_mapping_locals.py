class DynamicLocals:
    def __init__(self):
        self.calls = []
        self.count = 0

    def __getitem__(self, name):
        self.calls.append(name)
        if name == "x":
            self.count += 1
            return self.count
        raise KeyError(name)


locals_map = DynamicLocals()
print(eval("x + x", {"x": 100}, locals_map), locals_map.calls)
print(eval("y", {"y": 9}, locals_map), locals_map.calls)
print(eval("lambda: x", {"x": 7}, {"x": 9})())


class BrokenLocals:
    def __getitem__(self, name):
        raise ValueError("lookup failed")


try:
    eval("x", {}, BrokenLocals())
except ValueError as exc:
    print(type(exc).__name__, str(exc))
