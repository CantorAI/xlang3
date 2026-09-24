for order, raw in (("big", b"\xff\x7f"), ("little", b"\x7f\xff")):
    value = int.from_bytes(raw, order, signed=True)
    print(value == -129)
    print(value.to_bytes(2, order, signed=True) == raw)

payload = b"\x01\x02\x03\x04" * 262144
for order in ("big", "little"):
    value = int.from_bytes(payload, order)
    print(value.to_bytes(len(payload), order) == payload)
print(int.from_bytes(b"", "big") == 0)
