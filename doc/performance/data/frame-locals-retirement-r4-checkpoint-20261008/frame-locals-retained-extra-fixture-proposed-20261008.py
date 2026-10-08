import sys


def capture(fail):
    removed = 1
    value = 1
    frame = sys._getframe()
    retained = frame.f_locals
    assert retained["value"] == 1 and retained["removed"] == 1
    retained["extra"] = 17
    value = 2
    later = 42
    del removed
    if fail:
        raise LookupError(frame, retained)
    return frame, retained


frame, retained = capture(False)
assert retained["value"] == 2 and retained["later"] == 42 and retained["extra"] == 17
assert "removed" not in retained
assert frame.f_locals["later"] == 42
frame.clear()
try:
    capture(True)
except LookupError as error:
    frame, retained = error.args
    assert retained["value"] == 2 and retained["later"] == 42 and retained["extra"] == 17
    assert "removed" not in retained
    assert frame.f_locals["later"] == 42
    frame.clear()
print("PASS retained-mapping-extra-keys")
