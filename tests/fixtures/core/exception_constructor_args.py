class CustomError(Exception):
    def __init__(self, message):
        self.message = message


error = CustomError("detail")
print(error.args, str(error), error.message)
try:
    raise CustomError("raised")
except CustomError as error:
    print(error.args, str(error), error.message)


class EmptyInitError(Exception):
    def __init__(self, *args):
        pass


error = EmptyInitError("one", 2)
print(error.args, str(error))

# KeyError's common one-argument constructor keeps BaseException's args and
# str behavior; subclasses still take the normal inherited-initializer path.
for key_error in (KeyError(), KeyError("missing"), KeyError("left", "right")):
    print(key_error.args, str(key_error))

class DerivedKeyError(KeyError):
    pass

derived_key_error = DerivedKeyError("derived")
print(derived_key_error.args, str(derived_key_error))
