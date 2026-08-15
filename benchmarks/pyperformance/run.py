# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
import argparse
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SUPPORTED = ROOT / "supported.txt"


def read_supported():
    names = []
    if not SUPPORTED.exists():
        return names
    for line in SUPPORTED.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped and not stripped.startswith("#"):
            names.append(stripped)
    return names


def run_checked(command):
    completed = subprocess.run(command, text=True, check=False)
    if completed.returncode != 0:
        joined = " ".join(str(part) for part in command)
        raise SystemExit(f"command failed with exit code {completed.returncode}: {joined}")


def ensure_pyperformance(python):
    completed = subprocess.run(
        [python, "-m", "pyperformance", "--help"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        text=True,
        check=False,
    )
    if completed.returncode != 0:
        raise SystemExit(
            "pyperformance is not installed for this Python. "
            "Run: python -m pip install -r benchmarks/pyperformance/requirements.txt"
        )


def cmd_list(args):
    ensure_pyperformance(args.python)
    run_checked([args.python, "-m", "pyperformance", "list"])


def cmd_status(args):
    supported = read_supported()
    print("XLang3 pyperformance support")
    print("============================")
    if not supported:
        print("No pyperformance benchmarks are marked supported yet.")
        print("Use benchmarks/cases for current VM microbenchmarks.")
        return
    for name in supported:
        print(name)


def cmd_run_cpython(args):
    ensure_pyperformance(args.python)
    supported = read_supported()
    if args.benchmark:
        benchmarks = args.benchmark
    else:
        benchmarks = supported
    if not benchmarks:
        raise SystemExit("no benchmarks selected; pass --benchmark or add names to supported.txt")
    bench_arg = ",".join(benchmarks)
    output = Path(args.output).resolve()
    run_checked([args.python, "-m", "pyperformance", "run", "-b", bench_arg, "-o", str(output)])


def main():
    parser = argparse.ArgumentParser(description="XLang3 pyperformance integration helper.")
    sub = parser.add_subparsers(dest="command", required=True)

    list_parser = sub.add_parser("list", help="List benchmarks available in pyperformance.")
    list_parser.add_argument("--python", default=sys.executable)
    list_parser.set_defaults(func=cmd_list)

    status_parser = sub.add_parser("status", help="Show the XLang3 supported pyperformance subset.")
    status_parser.set_defaults(func=cmd_status)

    run_parser = sub.add_parser("run-cpython", help="Run selected pyperformance benchmarks on CPython.")
    run_parser.add_argument("--python", default=sys.executable)
    run_parser.add_argument("--benchmark", action="append", help="Benchmark name. Can be repeated.")
    run_parser.add_argument("--output", default=str(ROOT / "pyperformance-cpython.json"))
    run_parser.set_defaults(func=cmd_run_cpython)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
