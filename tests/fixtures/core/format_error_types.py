for value, specification in ((None, "0.1f"), ("x", "^5d")):
    try:
        format(value, specification)
    except Exception as error:
        print(type(error).__name__)
