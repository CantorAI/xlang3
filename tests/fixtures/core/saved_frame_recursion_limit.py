import sys


old_limit = sys.getrecursionlimit()
sys.setrecursionlimit(80)
entered = [False]


def recurse():
    return recurse()


def trace(frame, event, arg):
    if not entered[0]:
        entered[0] = True
        # This callback re-enters the VM while the traced frame is saved.
        # Recursion accounting must include that outer Python frame.
        sys.settrace(None)
        try:
            recurse()
        except RecursionError:
            print("nested callback recursion guarded")
    return None


def target():
    return None


sys.settrace(trace)
target()
sys.settrace(None)
sys.setrecursionlimit(old_limit)
