def make_cell_reader(value):

    def read():
        return value

    def write(new_value):
        nonlocal value
        value = new_value

    def clear():
        nonlocal value
        del value

    return read, write, clear


read_first, write_first, clear_first = make_cell_reader("first")
read_second, _, _ = make_cell_reader("second")
for _ in range(3):
    print(read_first())
    print(read_second())
write_first("updated")
print(read_first())
print(read_second())
clear_first()
try:
    read_first()
except NameError:
    print("unbound")
print(read_second())
