import os
import string
import sys


source_path = os.path.normcase(os.path.normpath(os.__file__))
print("os-process-source", source_path.endswith(os.path.normcase(os.path.normpath("Lib/os.py"))))

wait_status = os.spawnv(
    os.P_WAIT,
    sys.executable,
    [sys.executable, "-c", "raise SystemExit(9)"],
)
process = os.spawnv(
    os.P_NOWAIT,
    sys.executable,
    [sys.executable, "-c", "raise SystemExit(5)"],
)
waited_process, child_status = os.waitpid(process, 0)
print(
    "os-process-spawn",
    wait_status == 9,
    waited_process == process,
    child_status == 5 << 8,
    os.waitstatus_to_exitcode(child_status) == 5,
)

environment = dict(os.environ)
environment["XLANG3_SPAWNVE_AUDIT"] = "yes"
env_status = os.spawnve(
    os.P_WAIT,
    sys.executable,
    [
        sys.executable,
        "-c",
        "import os; raise SystemExit(0 if os.environ.get('XLANG3_SPAWNVE_AUDIT') == 'yes' else 4)",
    ],
    environment,
)
print("os-process-environment", env_status == 0)
print("os-process-system", os.system("exit 7") == 7)

missing_drive = next(
    letter for letter in reversed(string.ascii_uppercase)
    if not os.path.exists(letter + ":/")
)
missing_path = missing_drive + ":/xlang3_missing_startfile_target"
try:
    os.startfile(missing_path)
except FileNotFoundError as exc:
    print(
        "os-process-startfile-error",
        exc.errno == 2,
        exc.winerror == 15,
        exc.filename == missing_path,
    )

try:
    os.kill(2147483647, 15)
except OSError as exc:
    print(
        "os-process-kill-error",
        exc.errno == 22,
        exc.winerror == 87,
        exc.filename is None,
    )
