for value in (0.0, -0.0, 0.1, -2.5, 1.5, 2.0**100):
    print(value.as_integer_ratio())

for value in (float("inf"), float("nan")):
    try:
        value.as_integer_ratio()
    except Exception as error:
        print(type(error).__name__, str(error))
