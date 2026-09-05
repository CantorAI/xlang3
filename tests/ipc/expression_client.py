import sys
import os
endpoint = "lrpc:" + sys.argv[1]
import expression_srv as cantor thru endpoint

Task = cantor.Task
@Task(NPU=1 and OS == "Windows")
def work(value):
    return value + 1

assert work(41) == 42
conditions = cantor.captured()
assert cantor.evaluate(conditions, {"NPU": 2, "OS": "Windows"}) == [True, {"NPU": 1}]
assert cantor.evaluate(conditions, {"NPU": 0}) == [False, {}]
assert cantor.evaluate(conditions, {"NPU": 2, "OS": "Linux"}) == [False, {}]
assert cantor.evaluate(cantor.expression_roundtrip(conditions[0]), {"NPU": 1, "OS": "Windows"}) == [True, {"NPU": 1}]
offset = 7
def transferred(value):
    import os
    return [os.getpid(), value + offset]

copied = cantor.snapshot(transferred)
reference = cantor.identity(transferred)
offset = 100
reply = copied(35)
if reply[0] == os.getpid() or reply[1] != 42:
    raise RuntimeError("function graph did not execute with a captured global in the server")
if reference(1)[1] != 101:
    raise RuntimeError("ordinary function references lost their original identity")

def make_nested(start):
    def outer(delta):
        def inner():
            return start + delta + offset
        return inner()
    return outer

original_nested = make_nested(3)
if original_nested(2) != 105:
    raise RuntimeError("local transitive closure capture failed")
nested = cantor.snapshot(original_nested)
offset = 200
if nested(2) != 105:
    raise RuntimeError("nested function globals or closure were not preserved")
print("expression-ipc-ok")
