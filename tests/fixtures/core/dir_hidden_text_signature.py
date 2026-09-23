class Plain:
    def method(self):
        pass


value = Plain()
print("__text_signature__" in dir(value))
print(getattr(value, "__text_signature__", "missing"))
print("method" in dir(value))
print(object.__text_signature__)
