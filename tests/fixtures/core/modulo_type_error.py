for value in ([1, 2], {1, 2}, frozenset({1, 2})):
    try:
        value % 2
    except Exception as error:
        print(type(error).__name__, str(error))
