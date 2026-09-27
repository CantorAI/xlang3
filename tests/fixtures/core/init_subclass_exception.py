class Marker(Exception):
    pass


class Parent:
    def __init_subclass__(cls):
        raise Marker("subclass rejected")


try:
    class Child(Parent):
        pass
except Exception as exc:
    print(type(exc).__name__, str(exc))
