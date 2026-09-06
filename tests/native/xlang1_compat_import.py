import xlang1_compat_sample

print(xlang1_compat_sample.add(20, 22))
print(xlang1_compat_sample.make_list()[1])
print(xlang1_compat_sample.make_dict()["answer"])
print(xlang1_compat_sample.is_changed_event())

def on_changed(left, right):
    return left + right

cookie = xlang1_compat_sample.changed.subscribe(on_changed)
print(xlang1_compat_sample.fire_changed(30, 12))
print(xlang1_compat_sample.changed.fire(5, 7))
xlang1_compat_sample.changed.unsubscribe(cookie)
print(xlang1_compat_sample.fire_changed(1, 2))

counter = xlang1_compat_sample.Counter()
print(counter.add(5))
print(counter.add(7))
print(counter.total)
import xlang1_compat_sample as properties

caught_assertion = False
try:
    assert False, "assert-message regression"
except AssertionError:
    caught_assertion = True
if not caught_assertion:
    raise RuntimeError("assert with a message did not raise")

assert properties.property_count == 3, "initial field"
for i in range(100):
    properties.property_count = i
    assert properties.current_count == i, "live getter"
    assert properties.increment_count() == i + 1, "native mutation"
    assert properties.property_count == i + 1, "live field"
    properties.checked_count = i + 2
    assert properties.checked_count == i + 2, "lambda property"
try:
    properties.current_count = 99
    assert False, "read-only property was overwritten"
except AttributeError:
    pass
rejected = False
try:
    properties.checked_count = -1
except Exception:
    rejected = True
assert rejected, "setter validation was bypassed"
assert properties.checked_count == 101, "rejected setter changed field"
rejected = False
try:
    del properties.property_count
except Exception:
    rejected = True
assert rejected, "deletion accepted"
assert properties.property_count == 101, "deletion changed field"
clear_rejected = False
try:
    properties.__dict__.clear()
except Exception:
    clear_rejected = True
if not clear_rejected:
    raise RuntimeError("module clear bypassed native property protection")
assert properties.property_count == 101, "clear partially mutated module"
properties.property_count = -1
getter_rejected = False
try:
    unavailable = properties.current_count
except RuntimeError:
    getter_rejected = True
if not getter_rejected:
    raise RuntimeError("getter exception was lost")
getter_rejected = False
try:
    unavailable = getattr(properties, "current_count", "fallback")
except RuntimeError:
    getter_rejected = True
if not getter_rejected:
    raise RuntimeError("getattr default swallowed getter exception")
properties.property_count = 101
payload = {"items": [1, 2, 3]}
properties.property_object = payload
assert properties.property_object is payload, "object identity"
payload["items"].append(4)
assert properties.property_object["items"] == [1, 2, 3, 4], "object mutation"
def invoke_property():
    return properties.callback()

for i in range(10):
    properties.callback = properties.make_list
    assert invoke_property() == [1, "two"], "list callback"
    properties.callback = properties.make_dict
    assert invoke_property()["answer"] == 42, "dict callback"
from xlang1_compat_sample import current_count
assert current_count == 101, "from import getter"
from xlang1_compat_sample import *
assert current_count == 101, "wildcard import getter"
assert property_object is properties.property_object, "wildcard import identity"

constructed = properties.Variable(1, 2, option=payload)
snapshot = constructed.snapshot()
assert snapshot[0:2] == [1, 2], "variable constructor positional arguments"
assert snapshot[2] is payload, "variable constructor keyword identity"
assert properties.Variable().snapshot() == [], "empty variable constructor"
rejected = False
try:
    properties.Variable(unknown=1)
except RuntimeError:
    rejected = True
assert rejected, "variable constructor exception"

counter_field = properties.Counter()
counter_field.count = 40
assert counter_field.add(2) == 42, "class field property native write"
counter_field.payload = payload
assert counter_field.payload is payload, "class field property object identity"
assert properties.Counter().count == 0, "class field property instance isolation"
