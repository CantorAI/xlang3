def make_counter():
    x = 0
    def inc():
        nonlocal x
        x = x + 1
        return x
    return inc

counter = make_counter()
print(counter())
print(counter())
