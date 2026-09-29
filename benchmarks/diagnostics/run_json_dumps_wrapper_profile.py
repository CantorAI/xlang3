"""Run paired, instrumented json.dumps wrapper profiles on XLang3 and CPython 3.14.

This diagnostic adds Python timers around JSONEncoder methods, so its elapsed
times are not pyperf scores. Use it to compare call-path proportions and keep
the exact pyperformance 1.14 payload counts fixed.
"""
import argparse
import hashlib
import json
import pathlib
import re
import statistics
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = pathlib.Path(__file__).with_name("json_dumps_wrapper_profile.py")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def measure(command):
    result = subprocess.run(
        [*command, str(SOURCE)], cwd=ROOT, check=True,
        capture_output=True, text=True,
    )
    output = result.stdout
    return {
        "whole_ms": float(re.search(r"whole_ms ([0-9.]+)", output).group(1)),
        "encode_ms": float(re.search(r"encode 4001 ([0-9.]+)", output).group(1)),
        "iterencode_ms": float(re.search(r"iterencode 4001 ([0-9.]+)", output).group(1)),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xlang", type=pathlib.Path, default=ROOT / "build/Release/xlang3.exe")
    parser.add_argument("--python", type=pathlib.Path, default=pathlib.Path(sys.executable))
    parser.add_argument("--pairs", type=int, default=7)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    if args.pairs < 1:
        parser.error("--pairs must be positive")

    xlang = args.xlang.resolve(strict=True)
    python = args.python.resolve(strict=True)
    rows = []
    for index in range(args.pairs):
        order = [("xlang3", [str(xlang)]), ("cpython314", [str(python)])]
        if index % 2:
            order.reverse()
        row = {name: measure(command) for name, command in order}
        rows.append(row)

    metric_names = ("whole_ms", "encode_ms", "iterencode_ms")
    report = {
        "pairs": rows,
        "medians_ms": {
            name: {
                metric: statistics.median(row[name][metric] for row in rows)
                for metric in metric_names
            }
            for name in ("xlang3", "cpython314")
        },
        "source_sha256": digest(SOURCE),
        "xlang_exe_sha256": digest(xlang),
        "runtime_dll_sha256": digest(xlang.with_name("xlang3_runtime.dll")),
        "python": str(python),
    }
    text = json.dumps(report, indent=2) + "\n"
    if args.output:
        args.output.write_text(text, encoding="utf-8")
    print(text, end="")


if __name__ == "__main__":
    main()
