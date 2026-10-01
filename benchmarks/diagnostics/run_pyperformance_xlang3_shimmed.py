"""Run selected official pyperformance benchmarks with an XLang3 executable.

The harness reuses the installed pyperformance benchmark environment rather
than attempting to pip-install packages under XLang3. Add
``benchmarks/diagnostics/pyperf_compat`` to PYTHONPATH to disable unsupported
Windows-only priority/host-metadata hooks identically in both runtimes.
"""
import argparse
import fnmatch
import os
import subprocess
import sys
from pathlib import Path

import pyperformance.cli as cli
import pyperformance.run as perf_run
from pyperformance import _benchmark, _utils


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


def run_named_benchmark(benchmark, *positional, **keywords):
    global current_benchmark
    current_benchmark = benchmark.name
    try:
        return original_benchmark_run(benchmark, *positional, **keywords)
    finally:
        current_benchmark = None


_benchmark.Benchmark.run = run_named_benchmark


def run_with_timeout(argv, *, env=None, capture=None, verbose=True):
    if os.path.normcase(os.path.abspath(argv[0])) != os.path.normcase(runtime):
        return original_run_cmd(argv, env=env, capture=capture, verbose=verbose)
    child_env = dict(os.environ if env is None else env)
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
    timeout = args.case_timeout
    for pattern, seconds in args.case_timeout_override:
        if current_benchmark and fnmatch.fnmatchcase(current_benchmark, pattern):
            timeout = seconds
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        print(f"Full-case timeout: {current_benchmark} exceeded {timeout} seconds", flush=True)
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(process.pid)],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       check=False)
        stdout, stderr = process.communicate()
        return 124, stdout, stderr
    return process.returncode, stdout, stderr


_utils.run_cmd = run_with_timeout
if args.case_timeout_override:
    print("Full-case timeout overrides: " + ", ".join(
        f"{pattern}={seconds}s" for pattern, seconds in args.case_timeout_override), flush=True)
mode_args = {"fast": ["--fast"], "rigorous": ["--rigorous"],
             "debug": ["--debug-single-value"]}[args.mode]
sys.argv = ["pyperformance", "run", *mode_args, "--benchmarks", args.benchmarks,
            "--python", runtime, "--inherit-environ",
            "PYTHONPATH,PYTHONPYCACHEPREFIX", "--output", output]
parser, options = cli.parse_args()
benchmarks = cli._benchmarks_from_options(options)
cli.cmd_run(options, benchmarks)
