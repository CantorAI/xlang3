"""Order-balanced same-source A/B for Richards.run(1) on two XLang3 builds.

This bypasses only pyperf's worker setup via run_pyperformance_workload_direct;
the benchmark's Richards.run(1) function is unchanged. Its one-shot result is
diagnostic evidence, not a substitute for the official pyperformance sample.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import random
import statistics
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "benchmarks/diagnostics/run_pyperformance_workload_direct.py"
RESULT = re.compile(r"direct workload richards: True \(([0-9.]+) s\)")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def run(executable, workload, cache_root):
    env = os.environ.copy()
    key = hashlib.sha256(str(executable.resolve()).encode()).hexdigest()[:12]
    env["PYTHONPYCACHEPREFIX"] = str(cache_root / key)
    completed = subprocess.run(
        [str(executable), str(RUNNER), str(workload)], cwd=ROOT, env=env,
        capture_output=True, text=True, timeout=30,
    )
    if completed.returncode:
        raise RuntimeError(f"{executable} failed ({completed.returncode}): {completed.stderr}")
    match = RESULT.search(completed.stdout)
    if not match:
        raise RuntimeError(f"unexpected output from {executable}: {completed.stdout}")
    return float(match.group(1))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("parent", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("workload", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--repeats", type=int, default=21)
    parser.add_argument("--warmup", type=int, default=2)
    args = parser.parse_args()
    args.parent = args.parent.resolve(strict=True)
    args.candidate = args.candidate.resolve(strict=True)
    args.workload = args.workload.resolve(strict=True)
    cache_root = args.output.parent / (args.output.stem + "-pycache")
    cache_root.mkdir(parents=True, exist_ok=True)

    for index in range(args.warmup):
        order = (0, 1) if index % 2 == 0 else (1, 0)
        for side in order:
            run((args.parent, args.candidate)[side], args.workload, cache_root)

    parent_samples = []
    candidate_samples = []
    orders = []
    for index in range(args.repeats):
        order = ((0, 1), (1, 0)) if index % 2 == 0 else ((1, 0), (0, 1))
        pairs = []
        for sequence in order:
            pair = [None, None]
            for side in sequence:
                pair[side] = run((args.parent, args.candidate)[side], args.workload, cache_root)
            pairs.append(pair)
        parent_samples.append(math.sqrt(pairs[0][0] * pairs[1][0]))
        candidate_samples.append(math.sqrt(pairs[0][1] * pairs[1][1]))
        orders.append(pairs)
    ratios = [new / old for old, new in zip(parent_samples, candidate_samples)]
    rng = random.Random(314159)
    boot = sorted(statistics.median(rng.choices(ratios, k=len(ratios)))
                  for _ in range(10000))
    report = {
        "measurement": "order-balanced paired direct Richards.run(1) process time",
        "note": "diagnostic workload execution; not an official pyperf score",
        "parent": str(args.parent), "candidate": str(args.candidate),
        "workload": str(args.workload),
        "sha256": {"parent_exe": digest(args.parent),
                   "parent_runtime": digest(args.parent.with_name("xlang3_runtime.dll")),
                   "candidate_exe": digest(args.candidate),
                   "candidate_runtime": digest(args.candidate.with_name("xlang3_runtime.dll")),
                   "workload": digest(args.workload)},
        "repeats": args.repeats, "warmup": args.warmup,
        "parent_order_balanced_seconds": parent_samples,
        "candidate_order_balanced_seconds": candidate_samples,
        "candidate_over_parent_ratios": ratios,
        "parent_median_seconds": statistics.median(parent_samples),
        "candidate_median_seconds": statistics.median(candidate_samples),
        "candidate_over_parent_median": statistics.median(ratios),
        "candidate_over_parent_ratio_interval_95": [boot[249], boot[9749]],
        "order_pairs_seconds": orders,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"parent median: {report['parent_median_seconds'] * 1000:.3f} ms")
    print(f"candidate median: {report['candidate_median_seconds'] * 1000:.3f} ms")
    print(f"candidate/parent: {report['candidate_over_parent_median']:.4f}x")
    print(f"raw samples: {args.output}")


if __name__ == "__main__":
    main()
