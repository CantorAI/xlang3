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
