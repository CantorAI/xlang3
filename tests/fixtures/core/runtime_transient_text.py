import codecs
import gc
import json
import sys
import uuid

# Load optional codec/module state before measuring changing request data.
codecs.decode(b"warm", "utf-8")
uuid.uuid4().hex
gc.collect()
before = sys.getunicodeinternedsize()
for index in range(1000):
    expected = json.loads('"value_%08d"' % index)
    assert (" value_%08d " % index).strip() == expected
    assert ("value_%08d tail" % index).split()[0] == expected
    assert json.loads('"VALUE_%08d"' % index).lower() == expected
    assert "value_{:08d}".format(index) == expected
    assert (b"value_%08d" % index).decode("ascii") == expected
    assert codecs.decode(b"value_%08d" % index, "utf-8") == expected
    assert (index + 1000000).to_bytes(8, "big").hex() == format(index + 1000000, "016x")
    assert int(bin(index + 1000000), 2) == index + 1000000
    nonce = uuid.uuid4().hex
    assert len(nonce) == 32 and int(nonce, 16) >= 0
del expected, nonce
gc.collect()
print("runtime text released", sys.getunicodeinternedsize() - before < 64)
empty, letter = "", "A"
print("fixed singletons", sys.intern("") is empty, chr(65) is letter)

class Custom:
    def __format__(self, spec):
        return "custom:" + spec

print("custom dispatch", "{:xyz}".format(Custom()))
