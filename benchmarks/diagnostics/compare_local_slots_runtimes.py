"""Order-balanced comparison of the local-slot loop on XLang3 and CPython.

This executes the unchanged benchmark file and measures process wall time. It
is a diagnostic for the guarded local-loop optimizer, not a pyperformance
score; startup is included equally for every runtime.
"""
import argparse
import hashlib
import itertools
import json
import os
from pathlib import Path
import platform
import random
import statistics
import subprocess
import sys
import time


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "benchmarks/cases/local_slots.py"


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def identify(command):
    executable = Path(command[0]).resolve(strict=True)
    files = [executable]
    if executable.suffix.lower() == ".exe":
        files += sorted(executable.parent.glob("*xlang3*runtime*.dll"))
    return {"command": [str(executable), *command[1:]],
            "sha256": {item.name: sha256(item) for item in files}}


def run(command, script, cache_prefix):
    env = os.environ.copy()
    env["PYTHONPYCACHEPREFIX"] = str(cache_prefix)
    started = time.perf_counter()
    result = subprocess.run([*command, str(script)], cwd=ROOT, env=env,
                            capture_output=True, text=True, timeout=30)
    elapsed = time.perf_counter() - started
    if result.returncode:
        raise RuntimeError(f"{command[0]} failed: {result.stderr[-2000:]}")
    if result.stdout.strip().splitlines()[-1] != "5999990":
        raise RuntimeError(f"{command[0]} produced unexpected output: {result.stdout[-1000:]}")
    return elapsed


def median_interval(values):
    rng = random.Random(314159)
    samples = sorted(statistics.median(rng.choices(values, k=len(values)))
                     for _ in range(5000))
    return [samples[124], samples[4874]]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--current", type=Path, default=ROOT / "build/Release/xlang3.exe")
    parser.add_argument("--before", type=Path,
                        help="optional pre-change XLang3 executable for a direct A/B")
    parser.add_argument("--august", type=Path,
                        default=Path(r"D:\CantorAI\xlang3-perf-aug20\build\Release\xlang3.exe"))
    parser.add_argument("--cpython", type=Path,
                        default=Path(r"C:\Python\Python314\python.exe"))
    parser.add_argument("--repeats", type=int, default=21)
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    commands = {"current": [str(args.current)], "august": [str(args.august)],
                "cpython314": [str(args.cpython)]}
    if args.before is not None:
        commands["before"] = [str(args.before)]
    identities = {name: identify(command) for name, command in commands.items()}
    run_dir = ROOT / "scratch/performance/local-slots-timed"
    run_dir.mkdir(parents=True, exist_ok=True)
    script = run_dir / "local_slots_timed.py"
    script.write_text(SOURCE.read_text(encoding="utf-8"), encoding="utf-8")
    cache_root = run_dir / "pycache"
    cache_root.mkdir(exist_ok=True)

    for _ in range(args.warmup):
        for name, command in commands.items():
            run(command, script, cache_root / name)

    names = list(commands)
    orders = list(itertools.permutations(names))
    rows = []
    samples = {name: [] for name in names}
    paired_ratios = {"cpython314/current": [], "current/august": []}
    if "before" in commands:
        paired_ratios["current/before"] = []
    for index in range(args.repeats):
        order = orders[index % len(orders)]
        row = {"order": list(order)}
        for name in order:
            elapsed = run(commands[name], script, cache_root / name)
            row[name] = elapsed
            samples[name].append(elapsed)
        rows.append(row)
        paired_ratios["cpython314/current"].append(row["cpython314"] / row["current"])
        paired_ratios["current/august"].append(row["current"] / row["august"])
        if "before" in commands:
            paired_ratios["current/before"].append(row["current"] / row["before"])
    results = {}
    for name, values in samples.items():
        results[name] = {"median_ms": statistics.median(values) * 1000,
                         "samples_seconds": values}
    comparisons = {}
    for name, values in paired_ratios.items():
        comparisons[name] = {"median_ratio": statistics.median(values),
                             "ratio_95_interval": median_interval(values),
                             "paired_ratios": values}

    data = {"benchmark": "local_slots.py main()", "source_sha256": sha256(SOURCE),
            "method": "order-balanced process wall time; includes process startup",
            "host": platform.platform(), "machine": platform.node(),
            "runtimes": identities, "results": results,
            "comparisons": comparisons, "ordered_runs": rows}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
    for name, result in comparisons.items():
        print(f"{name}: {result['median_ratio']:.4f}x "
              f"95%=[{result['ratio_95_interval'][0]:.4f}, "
              f"{result['ratio_95_interval'][1]:.4f}]")
    print(f"saved: {args.output}")


if __name__ == "__main__":
    main()
