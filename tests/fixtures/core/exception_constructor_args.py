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
