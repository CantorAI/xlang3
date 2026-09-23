def retained_value(take_error_path):
    values = {"answer": 42}
    if take_error_path:
        raise ValueError(sorted([], key=lambda name: values.get(name)))
    return values["answer"]


print(retained_value(False))

try:
    retained_value(True)
except ValueError as exc:
    print(exc.args[0])
