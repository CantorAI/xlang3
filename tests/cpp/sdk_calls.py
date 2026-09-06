def task(a, *, Binder, InstanceId, **kwargs):
    Binder.append(InstanceId)
    return a + kwargs['extra']

class Worker:
    def run(self, value, *, offset):
        return value + offset

    def __call__(self, value, *, offset):
        return self.run(value, offset=offset)

worker = Worker()

class ConfiguredWorker:
    def __init__(self, *, offset):
        self.offset = offset

    def __call__(self, value):
        return value + self.offset

def broken(*, reason):
    raise ValueError(reason)

def check_native_view(view):
    import struct
    view[0] = 17
    view[-1] = 29
    assert view[0] == 17 and view[-1] == 29
    struct.pack_into('<I', view, 4, 0x12345678)
    assert struct.unpack_from('<I', view, 4)[0] == 0x12345678
    derived = memoryview(view)[4:8].toreadonly()
    view.release()
    return derived
