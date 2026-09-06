import sys
from concurrent.futures import ThreadPoolExecutor

assert sys.implementation.name == "cpython"

class Counter:
    def __init__(self, value):
        self.value = value

    def add(self, amount):
        self.value += amount
        return self.value

def invoke(callback, value):
    return callback(value, offset=3)

def threaded(callback, value):
    with ThreadPoolExecutor(max_workers=1) as pool:
        return pool.submit(lambda: callback(value, offset=3)).result(timeout=5)

def fail():
    raise ValueError("hosted CPython error")

def container():
    return {"values": [1, 2, 3], "binary": b"a\x00b"}

saved = None
def retain(callback):
    global saved
    saved = callback

def call_saved():
    return saved(39, offset=3)

def saved_is_closed():
    try:
        call_saved()
    except RuntimeError as error:
        return "closed" in str(error)
    return False
