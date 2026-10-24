"""Run selected official pyperformance benchmarks with an XLang3 executable.

The harness reuses the installed pyperformance benchmark environment rather
than attempting to pip-install packages under XLang3. Add
``benchmarks/diagnostics/pyperf_compat`` to PYTHONPATH to disable unsupported
Windows-only priority/host-metadata hooks identically in both runtimes.
"""
import argparse
import ctypes
import ctypes.wintypes
import fnmatch
import os
import re
import subprocess
import sys
import time
from pathlib import Path

import pyperformance.cli as cli
import pyperformance.run as perf_run
from pyperformance import _benchmark, _utils

from preserve_pyperformance_partial import preserve_partial_output


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--runtime", required=True, help="XLang3 executable")
parser.add_argument("--benchmarks", default="all", help="Comma-separated pyperformance names")
parser.add_argument("--mode", choices=("fast", "rigorous", "debug"), default="fast")
parser.add_argument("--output", required=True, help="pyperf JSON output")
parser.add_argument("--case-timeout", type=int, default=300)
def timeout_override(value):
    pattern, separator, seconds = value.partition("=")
    try:
        timeout = int(seconds)
    except ValueError:
        timeout = 0
    if not separator or not pattern or timeout <= 0:
        raise argparse.ArgumentTypeError("use BENCHMARK_PATTERN=POSITIVE_SECONDS")
    return pattern, timeout


parser.add_argument("--case-timeout-override", action="append", default=[],
                    type=timeout_override,
                    help="Override the full-case cap; glob patterns allowed, last match wins")
parser.add_argument("--dependency-site", action="append", default=[], type=Path,
                    help="Existing CPython benchmark site-packages directory; repeatable")
def worker_environment_name(value):
    if re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", value) is None:
        raise argparse.ArgumentTypeError("use a single environment-variable name")
    return value


parser.add_argument("--inherit-worker-env", action="append", default=[],
                    type=worker_environment_name,
                    help="Additional variable to inherit in timed workers (for explicit diagnostics)")
args = parser.parse_args()
if args.case_timeout <= 0:
    parser.error("--case-timeout must be positive")

runtime = os.path.abspath(args.runtime)
output = os.path.abspath(args.output)
dependency_sites = [str(path.resolve()) for path in args.dependency_site]
for path in dependency_sites:
    if not Path(path).is_dir():
        parser.error(f"dependency site is not a directory: {path}")
if dependency_sites:
    # Keep compatibility hooks first, then expose the same Python package
    # sources as the reference run. XLang3's native loader accepts its own
    # .x3pkg packages; adding this path does not install CPython extensions.
    paths = [os.environ.get("PYTHONPATH", ""), *dependency_sites]
    os.environ["PYTHONPATH"] = os.pathsep.join(path for path in paths if path)
    print("Benchmark dependency sites: " + os.pathsep.join(dependency_sites), flush=True)


class DirectXlangEnvironment:
    python = runtime

    def ensure_reqs(self, benchmark):
        # Dependencies belong to the shared CPython pyperformance environment
        # and are made visible to XLang3 through PYTHONPATH.
        return None


env = DirectXlangEnvironment()
perf_run.VenvForBenchmarks.ensure = classmethod(lambda cls, *a, **kw: env)
original_run_cmd = _utils.run_cmd
original_benchmark_run = _benchmark.Benchmark.run
current_benchmark = None
current_benchmark_deadline = None
current_benchmark_timeout_seconds = None
current_benchmark_timeout_reported = False


def _make_kill_job(process):
    """Put a Windows worker and its descendants in a kill-on-close job."""
    if os.name != "nt":
        return None

    class BasicLimit(ctypes.Structure):
        _fields_ = [
            ("PerProcessUserTimeLimit", ctypes.c_longlong),
            ("PerJobUserTimeLimit", ctypes.c_longlong),
            ("LimitFlags", ctypes.wintypes.DWORD),
            ("MinimumWorkingSetSize", ctypes.c_size_t),
            ("MaximumWorkingSetSize", ctypes.c_size_t),
            ("ActiveProcessLimit", ctypes.wintypes.DWORD),
            ("Affinity", ctypes.c_size_t),
            ("PriorityClass", ctypes.wintypes.DWORD),
            ("SchedulingClass", ctypes.wintypes.DWORD),
        ]

    class IoCounters(ctypes.Structure):
        _fields_ = [(name, ctypes.c_ulonglong) for name in (
            "ReadOperationCount", "WriteOperationCount", "OtherOperationCount",
            "ReadTransferCount", "WriteTransferCount", "OtherTransferCount")]

    class ExtendedLimit(ctypes.Structure):
        _fields_ = [
            ("BasicLimitInformation", BasicLimit),
            ("IoInfo", IoCounters),
            ("ProcessMemoryLimit", ctypes.c_size_t),
            ("JobMemoryLimit", ctypes.c_size_t),
            ("PeakProcessMemoryUsed", ctypes.c_size_t),
            ("PeakJobMemoryUsed", ctypes.c_size_t),
        ]

    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateJobObjectW.restype = ctypes.wintypes.HANDLE
    job = kernel.CreateJobObjectW(None, None)
    if not job:
        return None
    info = ExtendedLimit()
    info.BasicLimitInformation.LimitFlags = 0x00002000  # KILL_ON_JOB_CLOSE
    ok = kernel.SetInformationJobObject(
        job, 9, ctypes.byref(info), ctypes.sizeof(info))
    if not ok or not kernel.AssignProcessToJobObject(job, process._handle):
        kernel.CloseHandle(job)
        return None
    return kernel, job


def run_named_benchmark(benchmark, *positional, **keywords):
    global current_benchmark, current_benchmark_deadline
    global current_benchmark_timeout_seconds, current_benchmark_timeout_reported
    current_benchmark = benchmark.name
    # pyperf starts one child process per sample. The case cap belongs to the
    # complete Benchmark.run(), not each child; otherwise one slow benchmark
    # can consume N * timeout seconds while pyperf retries all of its samples.
    current_benchmark_timeout_seconds = args.case_timeout
    for pattern, seconds in args.case_timeout_override:
        if fnmatch.fnmatchcase(current_benchmark, pattern):
            current_benchmark_timeout_seconds = seconds
    current_benchmark_deadline = time.monotonic() + current_benchmark_timeout_seconds
    current_benchmark_timeout_reported = False
    try:
        return original_benchmark_run(benchmark, *positional, **keywords)
    finally:
        current_benchmark = None
        current_benchmark_deadline = None
        current_benchmark_timeout_seconds = None
        current_benchmark_timeout_reported = False


_benchmark.Benchmark.run = run_named_benchmark


def preserve_failed_output(argv, exit_code):
    # This runs only after failure/worker cleanup, never inside a timed body.
    # Upstream deletes --output's temporary file when Benchmark.run raises.
    try:
        record = preserve_partial_output(argv, output, current_benchmark, exit_code)
    except Exception as error:
        print(f"Could not preserve partial benchmark output: {error}", flush=True)
    else:
        if record is not None:
            print(f"Preserved partial failed-definition output: {record['partial_output']}", flush=True)


def run_with_timeout(argv, *, env=None, capture=None, verbose=True):
    global current_benchmark_timeout_reported
    if os.path.normcase(os.path.abspath(argv[0])) != os.path.normcase(runtime):
        return original_run_cmd(argv, env=env, capture=capture, verbose=verbose)
    remaining = current_benchmark_timeout_seconds or args.case_timeout
    if current_benchmark_deadline is not None:
        remaining = min(remaining, current_benchmark_deadline - time.monotonic())
    if remaining <= 0:
        if current_benchmark and not current_benchmark_timeout_reported:
            print(f"Full-case timeout: {current_benchmark} exceeded {current_benchmark_timeout_seconds} seconds", flush=True)
            current_benchmark_timeout_reported = True
        preserve_failed_output(argv, 124)
        return 124, "", ""
    child_env = dict(os.environ if env is None else env)
    if current_benchmark in {"python_startup", "python_startup_no_site"} and "-c" in argv:
        # The startup benchmark child only executes its command string; it is
        # not a pyperf worker. Match the CPython reference environment by
        # removing the worker-only sitecustomize hook from the timed process.
        compat_path = os.path.normcase(os.path.abspath(
            Path(__file__).parent / "pyperf_compat"))
        child_env["PYTHONPATH"] = os.pathsep.join(
            item for item in child_env.get("PYTHONPATH", "").split(os.pathsep)
            if item and os.path.normcase(os.path.abspath(item)) != compat_path)
    if capture is True:
        capture = "both"
    options = {"env": child_env}
    if capture == "both":
        options.update(stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    elif capture == "combined":
        options.update(stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    elif capture == "stdout":
        options.update(stdout=subprocess.PIPE)
    elif capture == "stderr":
        options.update(stderr=subprocess.PIPE)
    if capture:
        options["encoding"] = "utf-8"
    process = subprocess.Popen(argv, **options)
    kill_job = _make_kill_job(process)
    timeout = remaining
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        if current_benchmark and not current_benchmark_timeout_reported:
            print(f"Full-case timeout: {current_benchmark} exceeded {current_benchmark_timeout_seconds} seconds", flush=True)
            current_benchmark_timeout_reported = True
        if kill_job:
            kill_job[0].CloseHandle(kill_job[1])
            kill_job = None
        else:
            # Fallback for Windows environments that disallow job assignment.
            try:
                subprocess.run(["taskkill", "/F", "/T", "/PID", str(process.pid)],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                               check=False, timeout=5)
            except subprocess.TimeoutExpired:
                pass
            finally:
                if process.poll() is None:
                    process.kill()
        # A descendant can inherit the worker's output handles even after
        # taskkill reports success. Keep cleanup bounded so the case timeout
        # cannot turn into an unbounded wait in communicate().
        try:
            stdout, stderr = process.communicate(timeout=2)
        except subprocess.TimeoutExpired as cleanup_timeout:
            process.kill()
            try:
                stdout, stderr = process.communicate(timeout=1)
            except subprocess.TimeoutExpired:
                stdout = cleanup_timeout.output or ""
                stderr = cleanup_timeout.stderr or ""
                for stream in (process.stdout, process.stderr):
                    if stream is not None:
                        stream.close()
        preserve_failed_output(argv, 124)
        return 124, stdout, stderr
    finally:
        if kill_job:
            kill_job[0].CloseHandle(kill_job[1])
    if process.returncode != 0:
        preserve_failed_output(argv, process.returncode)
    return process.returncode, stdout, stderr


_utils.run_cmd = run_with_timeout
if args.case_timeout_override:
    print("Full-case timeout overrides: " + ", ".join(
        f"{pattern}={seconds}s" for pattern, seconds in args.case_timeout_override), flush=True)
if args.inherit_worker_env:
    print("Explicit worker environment variables: " + ", ".join(
        dict.fromkeys(args.inherit_worker_env)), flush=True)
mode_args = {"fast": ["--fast"], "rigorous": ["--rigorous"],
             "debug": ["--debug-single-value"]}[args.mode]
sys.argv = ["pyperformance", "run", *mode_args, "--benchmarks", args.benchmarks,
            "--python", runtime, "--inherit-environ",
            ",".join(dict.fromkeys([
                "PYTHONPATH", "PYTHONPYCACHEPREFIX", "XLANG3_PYTHON_LIB",
                *args.inherit_worker_env])), "--output", output]
parser, options = cli.parse_args()
benchmarks = cli._benchmarks_from_options(options)
cli.cmd_run(options, benchmarks)
