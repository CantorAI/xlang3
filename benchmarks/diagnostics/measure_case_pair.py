"""Order-balanced A/B measurement of one source against two XLang3 builds."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "benchmarks"))
from check_regression import assess, run_once, runtime_identity, timed_source  # noqa: E402


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repeats", type=int, default=21)
    parser.add_argument("--warmup", type=int, default=3)
    args = parser.parse_args()
    baseline = args.baseline.resolve(strict=True)
    candidate = args.candidate.resolve(strict=True)
    source_text = timed_source(args.source.read_text(encoding="utf-8"))
    samples = [[], []]
    positions = [[[], []], [[], []]]
    expected = None
    with tempfile.TemporaryDirectory(prefix="xlang3-case-pair-") as directory:
        timed_path = Path(directory) / args.source.name
        timed_path.write_text(source_text, encoding="utf-8")
        for index in range(args.warmup + args.repeats):
            orderings = ((0, 1), (1, 0))
            if index % 2:
                orderings = tuple(reversed(orderings))
            paired = []
            for order in orderings:
                pair = [None, None]
                for position, side in enumerate(order):
                    executable = (baseline, candidate)[side]
                    seconds, output = run_once(executable, timed_path, 120)
                    if expected is None:
                        expected = output
                    elif output != expected:
                        raise ValueError("benchmark output mismatch between builds")
                    pair[side] = seconds
                    if index >= args.warmup:
                        positions[side][position].append(seconds)
                paired.append(pair)
            if index >= args.warmup:
                # Geometric within-order pairs cancel launch-position bias.
                samples[0].append(math.sqrt(paired[0][0] * paired[1][0]))
                samples[1].append(math.sqrt(paired[0][1] * paired[1][1]))
    result = assess(samples[0], samples[1], 0.10)
    result.update({
        "measurement_method": f"{args.repeats} order-balanced paired samples; each measured pair runs both AB and BA orders",
        "baseline": runtime_identity(baseline),
        "candidate": runtime_identity(candidate),
        "source": str(args.source.resolve()),
        "source_sha256": hashlib.sha256(args.source.read_bytes()).hexdigest(),
        "stdout_sha256": hashlib.sha256(expected.encode()).hexdigest(),
        "order_balanced_seconds": {
            "baseline_first": positions[0][0],
            "baseline_second": positions[0][1],
            "candidate_first": positions[1][0],
            "candidate_second": positions[1][1],
        },
    })
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("median candidate/baseline: {:.4f}x (95% CI {:.4f}–{:.4f})".format(
        result["ratio"], *result["ratio_interval_95"]))
    print("baseline median: {:.3f} ms; candidate median: {:.3f} ms".format(
        result["baseline_median_ms"], result["candidate_median_ms"]))
    print(f"raw samples: {args.output}")


if __name__ == "__main__":
    main()
