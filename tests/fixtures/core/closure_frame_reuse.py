def make_offset(offset):
    def add(value):
        return value + offset

    return add


def invoke(function, value):
    return function(value)


low = make_offset(10)
high = make_offset(100)
for _ in range(100):
    assert invoke(low, 5) == 15
    assert invoke(high, 5) == 105

# The returned function is held only by the call arguments while its closure
# frame is active; frame reuse must not keep a stale closure or lose its cells.
transient_result = invoke(make_offset(900), 2)
assert transient_result == 902
print("closure-frame-reuse-ok", invoke(low, 5), invoke(high, 5), transient_result)
