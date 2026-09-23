class Parent(Exception):
    def __init__(self):
        super().__init__("ready")
        self.status_code = 413


class Child(Parent):
    pass


try:
    raise Child
except Child as exc:
    print(exc.status_code, str(exc))


class Broken(Exception):
    def __init__(self):
        raise ValueError("constructor failed")


try:
    raise Broken
except ValueError as exc:
    print(type(exc).__name__, str(exc))


try:
    raise object
except TypeError as exc:
    print(type(exc).__name__, str(exc))
