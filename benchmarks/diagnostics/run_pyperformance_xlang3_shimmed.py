"""Run selected official pyperformance benchmarks with an XLang3 executable.

The harness reuses the installed pyperformance benchmark environment rather
than attempting to pip-install packages under XLang3. Add
``benchmarks/diagnostics/pyperf_compat`` to PYTHONPATH to disable unsupported
Windows-only priority/host-metadata hooks identically in both runtimes.
"""
import argparse
import os
import subprocess
import sys

import pyperformance.cli as cli
import pyperformance.run as perf_run
from pyperformance import _utils


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--runtime", required=True, help="XLang3 executable")
parser.add_argument("--benchmarks", default="all", help="Comma-separated pyperformance names")
parser.add_argument("--mode", choices=("fast", "rigorous", "debug"), default="fast")
parser.add_argument("--output", required=True, help="pyperf JSON output")
parser.add_argument("--case-timeout", type=int, default=300)
args = parser.parse_args()

runtime = os.path.abspath(args.runtime)
output = os.path.abspath(args.output)


class DirectXlangEnvironment:
    python = runtime

    def ensure_reqs(self, benchmark):
        # Dependencies belong to the shared CPython pyperformance environment
        # and are made visible to XLang3 through PYTHONPATH.
        return None


env = DirectXlangEnvironment()
perf_run.VenvForBenchmarks.ensure = classmethod(lambda cls, *a, **kw: env)
original_run_cmd = _utils.run_cmd


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
    try:
        stdout, stderr = process.communicate(timeout=args.case_timeout)
    except subprocess.TimeoutExpired:
        subprocess.run(["taskkill", "/F", "/T", "/PID", str(process.pid)],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       check=False)
        stdout, stderr = process.communicate()
        return 124, stdout, stderr
    return process.returncode, stdout, stderr


_utils.run_cmd = run_with_timeout
mode_args = {"fast": ["--fast"], "rigorous": ["--rigorous"],
             "debug": ["--debug-single-value"]}[args.mode]
sys.argv = ["pyperformance", "run", *mode_args, "--benchmarks", args.benchmarks,
            "--python", runtime, "--inherit-environ",
            "PYTHONPATH,PYTHONPYCACHEPREFIX", "--output", output]
parser, options = cli.parse_args()
benchmarks = cli._benchmarks_from_options(options)
cli.cmd_run(options, benchmarks)
