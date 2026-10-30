import gc
import json
import threading
import types


def make_pair():
    def left():
        return right

    def right():
        return left

    left.gc_cycle_marker = 'unseeded-function-cycle'
    right.gc_cycle_marker = 'unseeded-function-cycle'
    return left


def tagged_functions():
    return sum(1 for obj in gc.get_objects()
               if type(obj) is types.FunctionType
               and getattr(obj, 'gc_cycle_marker', None) == 'unseeded-function-cycle')


def discard_pairs(number):
    for _ in range(number):
        make_pair()


gc.collect()
baseline = tagged_functions()
for _ in range(4):
    discard_pairs(1000)
    assert gc.collect() >= 0
    assert tagged_functions() == baseline
print('PASS unseeded-mutual-closures-reclaimed-repeatedly')


def exercise_live_pair():
    live = make_pair()
    gc.collect()
    assert live()() is live
    assert tagged_functions() == baseline + 2


exercise_live_pair()
gc.collect()
assert tagged_functions() == baseline
print('PASS live-closure-remains-callable')


def make_defaults_cycle():
    def callback(value=None, *, keyword=None):
        return value, keyword

    callback.gc_cycle_marker = 'unseeded-function-cycle'
    callback.__defaults__ = (callback,)
    callback.__kwdefaults__ = {'keyword': callback}
    callback.__annotations__ = {'return': callback}
    return callback


def exercise_live_defaults():
    defaults = make_defaults_cycle()
    gc.collect()
    assert defaults() == (defaults, defaults)


exercise_live_defaults()
gc.collect()
assert tagged_functions() == baseline
print('PASS defaults-keyword-annotations-owning-multiplicity')


class Receiver:
    def echo(self):
        return self


def discard_bound_method_cycle():
    callback = Receiver().echo

    def recursive():
        return recursive, callback

    recursive.gc_cycle_marker = 'unseeded-function-cycle'


for _ in range(1000):
    discard_bound_method_cycle()
gc.collect()
assert tagged_functions() == baseline
print('PASS closure-bound-method-instance-cycle')


def discard_builtin_capture_cycle():
    predicate = isinstance
    value_type = int

    def recursive(value):
        return recursive if predicate(value, value_type) else None

    recursive.gc_cycle_marker = 'unseeded-function-cycle'


for _ in range(1000):
    discard_builtin_capture_cycle()
gc.collect()
assert tagged_functions() == baseline
print('PASS captured-types-and-intrinsic-builtins')


def encode_responses(number):
    for _ in range(number):
        assert json.dumps({'ok': True}, ensure_ascii=False, allow_nan=False,
                          separators=(',', ':')) == '{"ok":true}'


def json_encoder_closures():
    return sum(1 for obj in gc.get_objects()
               if type(obj) is types.FunctionType
               and '_make_iterencode.<locals>' in obj.__qualname__)


gc.collect()
encoder_baseline = json_encoder_closures()
for _ in range(3):
    encode_responses(1000)
    gc.collect()
    assert json_encoder_closures() == encoder_baseline
for allow_nan in (True, False):
    if allow_nan:
        assert json.dumps(float('nan'), allow_nan=allow_nan) == 'NaN'
    else:
        try:
            json.dumps(float('nan'), allow_nan=allow_nan)
        except ValueError:
            pass
        else:
            raise AssertionError('allow_nan=False accepted NaN')
print('PASS repeated-json-response-encoder-closures')


ready = threading.Event()
release = threading.Event()
errors = []


def hold_closure():
    value = make_pair()
    ready.set()
    release.wait()
    if value()() is not value:
        errors.append('collected live thread closure')


worker = threading.Thread(target=hold_closure)
worker.start()
ready.wait()
gc.collect()
release.set()
worker.join()
assert not errors
gc.collect()
assert tagged_functions() == baseline
print('PASS other-thread-closure-root')
