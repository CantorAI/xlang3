def outer():
    x = 10
    def inner():
        return x
    return inner()

def make_reader():
    x = 33
    def read():
        return x
    return read

reader = make_reader()
print(outer())
print(reader())
