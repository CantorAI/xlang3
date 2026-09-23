def reraised_exception():
    try:
        raise ValueError("same")
    except ValueError as caught:
        try:
            raise caught
        except ValueError as reraised:
            print(reraised is caught)
            print(reraised.__context__ is reraised)
            print(reraised.__context__)


reraised_exception()
