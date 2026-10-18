import errno
import os
import socket
import stat
import subprocess
import sys
import tempfile


print("os-process-source", os.path.isfile(os.__file__) and os.path.basename(os.__file__) == "os.py")
read_fd, write_fd = os.pipe()
left, right = socket.socketpair()
try:
    pipe_stat = os.fstat(read_fd)
    socket_stat = os.fstat(left.fileno())
    print("os-process-descriptor-kinds", stat.S_ISFIFO(pipe_stat.st_mode),
          stat.S_ISSOCK(socket_stat.st_mode), pipe_stat[0] == pipe_stat.st_mode)
finally:
    os.close(read_fd)
    os.close(write_fd)
    left.close()
    right.close()

waited = subprocess.run([sys.executable, "-c", "raise SystemExit(9)"])
with subprocess.Popen([sys.executable, "-c", "raise SystemExit(5)"]) as child:
    waited_pid, status = os.waitpid(child.pid, 0)
    child.returncode = os.waitstatus_to_exitcode(status)
    print("os-process-spawn", waited.returncode == 9, waited_pid == child.pid,
          status == 5 << 8, child.returncode == 5)

environment = dict(os.environ)
environment["XLANG3_SPAWNVE_AUDIT"] = "yes"
result = subprocess.run(
    [sys.executable, "-c", "import os; raise SystemExit(0 if os.environ.get('XLANG3_SPAWNVE_AUDIT') == 'yes' else 4)"],
    env=environment,
)
print("os-process-environment", result.returncode == 0)
print("os-process-shell", subprocess.run("exit 7", shell=True).returncode == 7)

with tempfile.TemporaryDirectory(prefix="xlang3-process-") as directory:
    path = os.path.join(directory, "missing-executable")
    try:
        subprocess.run([path])
    except FileNotFoundError as exc:
        print("os-process-exec-error", exc.errno == errno.ENOENT, exc.filename == path)

try:
    os.kill(2147483647, 0)
except OSError as exc:
    print("os-process-kill-error", type(exc) is ProcessLookupError,
          exc.errno == errno.ESRCH, len(exc.args) == 2, exc.filename is None)

try:
    os.waitpid(2147483647, 0)
except OSError as exc:
    print("os-process-wait-error", type(exc) is ChildProcessError,
          exc.errno == errno.ECHILD, len(exc.args) == 2, exc.filename is None)
