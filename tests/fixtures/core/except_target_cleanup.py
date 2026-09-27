import sys


def reraised():
    try:
        raise ValueError('outer')
    except ValueError as err:
        raise RuntimeError('inner') from err


def returned():
    try:
        raise ValueError('return')
    except ValueError as err:
        return sys._getframe()


def loop_exits():
    for _ in range(1):
        try:
            raise ValueError('break')
        except ValueError as err:
            break
    print('break cleared:', 'err' not in sys._getframe().f_locals)
    for index in range(2):
        try:
            raise ValueError('continue')
        except ValueError as err:
            continue
    print('continue cleared:', 'err' not in sys._getframe().f_locals)


try:
    reraised()
except RuntimeError as caught:
    trace = caught.__traceback__
    while trace is not None and trace.tb_frame.f_code.co_name != 'reraised':
        trace = trace.tb_next
    print('raise cleared:', trace is not None and 'err' not in trace.tb_frame.f_locals)

frame = returned()
print('return cleared:', 'err' not in frame.f_locals)
loop_exits()
