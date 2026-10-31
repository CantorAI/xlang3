import gc
import json
import sys

gc.collect()
before = sys.getunicodeinternedsize()
for index in range(3000):
    document = '{"transient_key_%08d":"transient_value_%08d"}' % (index, index)
    value = json.loads(document)
    assert len(value) == 1
    assert json.loads(json.dumps(value)) == value
del document, value
gc.collect()
print("transient strings released", sys.getunicodeinternedsize() - before < 64)

before = sys.getunicodeinternedsize()
for number in range(1000000, 1002000):
    assert repr(number) == int.__repr__(number) == str(number)
    assert json.loads(json.dumps({"count": number, "fraction": number + 0.25}, indent=2))["count"] == number
    assert repr(number << 100) == int.__repr__(number << 100)
gc.collect()
print("numeric rendering released", sys.getunicodeinternedsize() - before < 64)

value = json.loads('{"a":"\\u0061bc", "b":"a\\u0000b", "c":"", "d":"雪"}')
print("contents preserved", value == {"a": "abc", "b": "a\x00b", "c": "", "d": "雪"})
print("explicit interning", sys.intern("explicit_name_" + str(42)) is sys.intern("explicit_name_" + str(42)))
