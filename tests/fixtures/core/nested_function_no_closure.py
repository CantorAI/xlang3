def outer():
    def inner():
        return 123
    return inner()

print(outer())
