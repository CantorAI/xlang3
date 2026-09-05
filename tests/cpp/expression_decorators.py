import xlang1_compat_sample as cantor

@cantor.Task(ThinkPad == 1)
def hello(value):
    return value + 1

assert hello(41) == 42
condition = cantor.captured()[0]
assert cantor.evaluate(condition, {"ThinkPad": 1}) == [True, {}]
assert cantor.evaluate(condition, {"ThinkPad": 0}) == [False, {}]

Task = cantor.Task
@Task(NPU=1 and OS == "Windows")
def work():
    return 42

condition = cantor.captured()[0]
snapshot = {"NPU": 2, "OS": "Windows"}
info = cantor.expression_info(condition)
assert info["op"] == "and"
reservation = cantor.expression_info(info["children"][0])
assert reservation["op"] == "reserve" and reservation["value"] == "NPU"
assert cantor.evaluate(condition, snapshot) == [True, {"NPU": 1}]
assert snapshot == {"NPU": 2, "OS": "Windows"}
assert cantor.evaluate(condition, {"NPU": 2, "OS": "Linux"}) == [False, {}]
assert cantor.evaluate(condition, {"NPU": 0}) == [False, {}]
restored = cantor.expression_roundtrip(condition)
assert cantor.evaluate(restored, snapshot) == [True, {"NPU": 1}]
assert work() == 42

@Task(CPU=0.1)
def fractional():
    pass

assert cantor.evaluate(cantor.captured()[0], {"CPU": 0.5}) == [True, {"CPU": 0.1}]

@Task(False and UndefinedResource == 1)
def short_circuit():
    pass

assert cantor.evaluate(cantor.captured()[0], {}) == [False, {}]

trace = []
def ordinary(value):
    trace.append(value)
    def decorate(fn):
        trace.append("apply" + str(value))
        return fn
    return decorate

@ordinary(1)
@ordinary(2)
def normal():
    return 17

assert trace == [1, 2, "apply2", "apply1"]
assert normal() == 17

def plain(**kwargs):
    assert kwargs == {"NPU": True}
    return lambda fn: fn

OS = "Windows"
@plain(NPU=1 and OS == "Windows")
def plain_keywords():
    pass

@Task(NPU=1 and OS == "Linux" or True)
def fallback():
    pass

assert cantor.evaluate(cantor.captured()[0], snapshot) == [True, {}]

@Task(NPU=1, CPU=0.5, OS == "Windows")
def multiple():
    pass

assert cantor.evaluate(cantor.captured(), {"NPU": 2, "CPU": 1, "OS": "Windows"}) == [True, {"NPU": 1, "CPU": 0.5}]
assert cantor.evaluate(cantor.captured(), {"NPU": 2, "CPU": 0, "OS": "Windows"}) == [False, {}]

@Task(1 < CPU <= 4)
def chained():
    pass

assert cantor.evaluate(cantor.captured()[0], {"CPU": 2}) == [True, {}]
assert cantor.evaluate(cantor.captured()[0], {"CPU": 5}) == [False, {}]

@Task(NPU=1 and NPU == 1)
def remaining_capacity():
    pass

assert cantor.evaluate(cantor.captured()[0], {"NPU": 2}) == [True, {"NPU": 1}]

@Task(*UndefinedArguments)
def unsupported_expansion():
    pass

try:
    cantor.evaluate(cantor.captured()[0], {})
    assert False, "captured argument expansion must not be silently accepted"
except RuntimeError:
    pass

@Task(**UndefinedKeywords)
def unsupported_keyword_expansion():
    pass

try:
    cantor.evaluate(cantor.captured()[0], {})
    assert False, "captured keyword expansion must not be silently accepted"
except RuntimeError:
    pass
