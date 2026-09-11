import _sysconfig
import os
import sys


modulus = sys.hash_info.modulus
print(
    "sys-hash-config",
    all(pow(value, modulus - 1, modulus) == 1 for value in range(1, 100)),
    _sysconfig.config_vars()["Py_HASH_ALGORITHM"] == 0,
    sys.hash_info.algorithm == "xlang3",
)
print(
    "sys-stdlib-config",
    os.path.normcase(sys._stdlib_dir) == os.path.normcase(os.path.dirname(os.__file__)),
    os.path.basename(os.__file__) == "os.py",
)

original_displayhook = sys.displayhook
displayed = []
sys.displayhook = lambda value: displayed.append(value)
eval(compile("42", "<displayhook-fixture>", "single"))
del sys.displayhook
try:
    eval(compile("43", "<displayhook-fixture>", "single"))
except RuntimeError as exc:
    lost_displayhook = str(exc) == "lost sys.displayhook"
sys.displayhook = original_displayhook
print("sys-single-displayhook", displayed == [42], lost_displayhook)

interned_source = "runtime intern identity with spaces"
interned_copy = interned_source.swapcase().swapcase()
print(
    "sys-intern-identity",
    sys.intern(interned_source) is interned_source,
    sys.intern(interned_copy) is interned_source,
    sys._is_interned(interned_copy) is False,
)


def frame_identity_probe():
    return sys._getframe().f_code is frame_identity_probe.__code__


print(
    "sys-runtime-details",
    frame_identity_probe(),
    10000000000000000000000000000000000000000 == 10**40,
    sys.stdin.errors == "surrogateescape",
    sys.stdout.errors == "surrogateescape",
    sys.stderr.errors == "backslashreplace",
)
