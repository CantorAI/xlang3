error = ValueError("bad value")
print(vars(error))
print(error.args, error.__traceback__, error.__cause__, error.__context__)

error.extra = 42
print(vars(error))
print(error.extra)

try:
    raise error
except ValueError as caught:
    print(sorted(vars(caught).items()))
    print(caught.__traceback__ is not None)
