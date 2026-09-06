data = bytearray(b"abcd")

def get_data():
    return data

def get_view():
    return memoryview(data)

def mutate(value):
    value[0] = 90
    value[-1] = 89
    return value[0]

def resize():
    data.append(33)

def same_size():
    data[1:3] = b"xy"

def check_pinned():
    try:
        data.append(1)
    except BufferError:
        return True
    return False

def cast_view():
    return memoryview(bytearray(b"\x01\x00\x00\x00")).cast("I")
