import os
import signal
import subprocess

assert all(isinstance(key, bytes) and isinstance(value, bytes)
           for key, value in os.environb.items())
os.environ["XLANG3_PARENT_TEST"] = "parent-env"
try:
    assert os.environb[b"XLANG3_PARENT_TEST"] == b"parent-env"
    inherited = subprocess.run(["/bin/sh", "-c", "printf '%s' \"$XLANG3_PARENT_TEST\""],
                               capture_output=True)
    assert inherited.stdout == b"parent-env", ("inherited environment", inherited.returncode, inherited.stdout, inherited.stderr)
finally:
    del os.environ["XLANG3_PARENT_TEST"]
assert b"XLANG3_PARENT_TEST" not in os.environb

result = subprocess.run(["/bin/sh", "-c", "printf hello; printf error >&2; exit 7"],
                        capture_output=True)
assert result.returncode == 7
assert result.stdout == b"hello"
assert result.stderr == b"error"

result = subprocess.run(["/bin/sh", "-c", "cat; printf error >&2"],
                        input="text-input", capture_output=True, text=True,
                        encoding="utf-8", errors="strict")
assert result.stdout == "text-input", ("text stdout", result.stdout)
assert result.stderr == "error", ("text stderr", result.stderr)

read_fd, write_fd = os.pipe()
try:
    assert os.write(write_fd, memoryview(b"01234")[1:4]) == 3
    assert os.read(read_fd, 3) == b"123"
finally:
    os.close(read_fd)
    os.close(write_fd)

result = subprocess.run(["/bin/sh", "-c", "printf '%s' \"$XLANG3_CHILD_TEST\""],
                        env={"XLANG3_CHILD_TEST": "child-env"}, capture_output=True,
                        cwd="/tmp", start_new_session=True)
assert result.returncode == 0, ("child status", result.returncode, result.stderr)
assert result.stdout == b"child-env", ("explicit environment", result.stdout)

read_fd, write_fd = os.pipe()
try:
    child = subprocess.Popen(["/bin/sh", "-c", "printf passed >&" + str(write_fd)],
                             pass_fds=(write_fd,))
    os.close(write_fd)
    write_fd = -1
    data = os.read(read_fd, 64)
    assert data == b"passed", ("pass_fds", data)
    assert child.wait() == 0, ("pass_fds status", child.returncode)
finally:
    os.close(read_fd)
    if write_fd >= 0:
        os.close(write_fd)

try:
    subprocess.run(["/xlang3-no-such-executable"])
except OSError:
    pass
else:
    raise AssertionError("missing executable succeeded")

child = subprocess.Popen(["/bin/sleep", "30"])
child.terminate()
status = child.wait()
assert status == -signal.SIGTERM, ("terminate status", status)
print("posix-subprocess-passed")
