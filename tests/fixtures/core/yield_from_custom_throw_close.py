class Delegate:
    def __init__(self): self.step = 0
    def __iter__(self): return self
    def __next__(self):
        self.step += 1
        return 'next' if self.step == 1 else self.finish()
    def finish(self): raise StopIteration('return')
    def send(self, value): return 'sent:' + str(value)
    def throw(self, exception):
        print('delegate-throw', type(exception).__name__)
        return 'recovered'
    def close(self): print('delegate-close')

def outer():
    result = yield from Delegate()
    return result
for mode in ('throw', 'close'):
    item = outer()
    print(mode, next(item))
    try:
        if mode == 'throw':
            print(mode, item.throw(ValueError('boom')))
            print(mode, next(item))
        else:
            print(mode, item.close())
    except BaseException as exc:
        print(mode, type(exc).__name__, getattr(exc, 'value', None))
