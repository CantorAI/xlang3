"""Compare two Release runtimes on identical inputs on this machine.

Exit 0: pass, 1: confirmed regression, 2: inconclusive or invalid run.
This is deliberately separate from compilation and correctness tests.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import random
import statistics
import subprocess
import sys
import tempfile


CASES = (
    "local_slots", "scalar_arithmetic", "range_for", "function_calls",
    "class_construct", "list_append", "property_access", "deepcopy_memo",
    "json_dumps", "gc_traversal", "subparsers",
)
MARKER = "__xlang3_benchmark_seconds__"
ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def runtime_identity(executable):
    path = Path(executable).resolve(strict=True)
    files = [path]
    # Hash the runtime too: the CLI executable alone does not identify a build.
    files += sorted(path.parent.glob("*xlang3*runtime*.dll"))
    files += sorted(path.parent.glob("*xlang3*runtime*.so*"))
    files += sorted(path.parent.glob("*xlang3*runtime*.dylib"))
    return {"executable": str(path), "sha256": {f.name: digest(f) for f in files}}


def timed_source(source):
    head, separator, tail = source.rpartition("\nmain()")
    if not separator or tail.strip():
        raise ValueError("benchmark must end with a standalone main() call")
    return (head + "\nimport time as _benchmark_time\n"
            "_benchmark_start = _benchmark_time.perf_counter()\nmain()\n"
            f"print('{MARKER}', _benchmark_time.perf_counter() - _benchmark_start)\n")


def run_once(executable, source, timeout):
    # Different XLang3 revisions may use different IR instruction layouts for
    # the same 3.14 bytecode cache. Keep each executable's startup cache apart
    # during paired comparisons so one side cannot consume the other's artifacts.
    cache_root = Path(os.environ.get(
        "PYTHONPYCACHEPREFIX", ROOT / "scratch/performance/pycache-regression"))
    runtime_dll = executable.with_name("xlang3_runtime.dll")
    identity = [str(executable.resolve()), executable.stat().st_mtime_ns]
    if runtime_dll.exists():
        identity.extend((str(runtime_dll.resolve()), runtime_dll.stat().st_mtime_ns))
    cache_key = hashlib.sha256(repr(identity).encode()).hexdigest()[:16]
    child_env = os.environ.copy()
    child_env["PYTHONPYCACHEPREFIX"] = str(cache_root / cache_key)
    completed = subprocess.run(
        [str(executable), str(source)], cwd=ROOT, capture_output=True,
        text=True, timeout=timeout, env=child_env,
    )
    if completed.returncode:
        raise ValueError(f"{executable} failed ({completed.returncode}): {completed.stderr[-4000:]}")
    lines = completed.stdout.splitlines()
    if not lines or not lines[-1].startswith(MARKER + " "):
        raise ValueError(f"{executable}: missing internal timing result")
    elapsed = float(lines[-1].split()[-1])
    if not math.isfinite(elapsed) or elapsed <= 0:
        raise ValueError(f"invalid elapsed time: {elapsed}")
    return elapsed, "\n".join(lines[:-1])


def assess(baseline, candidate, threshold):
    """A paired bootstrap interval distinguishes a slowdown from timing noise."""
    if len(baseline) != len(candidate) or len(baseline) < 7:
        raise ValueError("need at least seven paired samples")
    if any(not math.isfinite(t) or t <= 0 for t in baseline + candidate):
        raise ValueError("samples must be finite and positive")
    ratios = [new / old for old, new in zip(baseline, candidate)]
    rng = random.Random(314159)
    boot = sorted(statistics.median(rng.choices(ratios, k=len(ratios)))
                  for _ in range(2000))
    low, high = boot[49], boot[1949]
    limit = 1 + threshold
    status = "pass" if high <= limit else "regression" if low > limit else "inconclusive"
    return {
        "status": status, "ratio": statistics.median(ratios),
        "ratio_interval_95": [low, high],
        "baseline_median_ms": statistics.median(baseline) * 1000,
        "candidate_median_ms": statistics.median(candidate) * 1000,
        "baseline_seconds": baseline, "candidate_seconds": candidate,
    }


def measure_case(baseline, candidate, source, repeats, warmup, timeout, threshold):
    expected = None
    samples = [[], []]
    positions = [[[], []], [[], []]]  # side, then first/second process position

    def run_order(order, collect):
        nonlocal expected
        pair = [None, None]
        for position, side in enumerate(order):
            elapsed, output = run_once((baseline, candidate)[side], source, timeout)
            if expected is None:
                expected = output
            if output != expected:
                raise ValueError(f"benchmark output mismatch: {source.name}")
            pair[side] = elapsed
            if collect:
                positions[side][position].append(elapsed)
        return pair

    # Each repeat observes each runtime in both process positions. Pairing only
    # alternating AB/BA runs confounds runtime identity with the position effect
    # when launch, timer, or host scheduling makes the first process faster.
    for index in range(warmup + repeats):
        orderings = ((0, 1), (1, 0))
        if index % 2:
            orderings = tuple(reversed(orderings))
        paired = [run_order(order, index >= warmup) for order in orderings]
        if index < warmup:
            continue
        # Use the two within-order comparisons to cancel first/second-position
        # bias. Their geometric mean is one order-balanced paired observation.
        baseline_effective = math.sqrt(paired[0][0] * paired[1][0])
        candidate_effective = math.sqrt(paired[0][1] * paired[1][1])
        samples[0].append(baseline_effective)
        samples[1].append(candidate_effective)
    result = assess(*samples, threshold)
    result["stdout_sha256"] = hashlib.sha256(expected.encode()).hexdigest()
    result["order_balanced_seconds"] = {
        "baseline_first": positions[0][0],
        "baseline_second": positions[0][1],
        "candidate_first": positions[1][0],
        "candidate_second": positions[1][1],
    }
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True, type=Path)
    parser.add_argument("--candidate", type=Path, default=ROOT / "build/Release/xlang3.exe")
    parser.add_argument("--output", type=Path, default=ROOT / "scratch/performance/latest.json")
    parser.add_argument("--repeats", type=int, default=21)
    parser.add_argument("--warmup", type=int, default=5)
    parser.add_argument("--threshold", type=float, default=0.10,
                        help="allowed fractional slowdown per case (default 0.10)")
    parser.add_argument("--timeout", type=float, default=60)
    parser.add_argument("--cases", nargs="+", choices=CASES, default=list(CASES))
    args = parser.parse_args(argv)
    if (args.repeats < 7 or args.warmup < 1 or not math.isfinite(args.threshold)
            or not 0 <= args.threshold < 1 or not math.isfinite(args.timeout) or args.timeout <= 0):
        parser.error("use repeats >= 7, warmup >= 1, 0 <= threshold < 1, and a positive timeout")
    report = {"schema": 2, "host": platform.platform(), "machine": platform.node(),
              "threshold": args.threshold, "repeats": args.repeats,
              "warmup": args.warmup,
              "measurement_method": f"{args.repeats} order-balanced paired samples; each measured pair runs both baseline-candidate and candidate-baseline orders",
              "cases": {}, "status": "invalid"}
    code = 2
    try:
        args.baseline = args.baseline.resolve(strict=True)
        args.candidate = args.candidate.resolve(strict=True)
        report["baseline"] = runtime_identity(args.baseline)
        report["candidate"] = runtime_identity(args.candidate)
        if args.baseline == args.candidate:
            raise ValueError("baseline must be a separate preserved runtime, not the candidate path")
        with tempfile.TemporaryDirectory(prefix="xlang3-performance-") as temporary:
            for case in args.cases:
                original = ROOT / "benchmarks/cases" / (case + ".py")
                source = Path(temporary) / (case + ".py")
                source.write_text(timed_source(original.read_text(encoding="utf-8")), encoding="utf-8")
                attempts = [measure_case(args.baseline, args.candidate, source, args.repeats,
                                         args.warmup, args.timeout, args.threshold)]
                if attempts[0]["status"] != "pass":
                    print(f"{case}: confirming {attempts[0]['status']} with a fresh run", flush=True)
                    attempts.append(measure_case(args.baseline, args.candidate, source, args.repeats,
                                                 args.warmup, args.timeout, args.threshold))
                states = [attempt["status"] for attempt in attempts]
                status = ("pass" if states == ["pass"] else
                          "regression" if states == ["regression", "regression"] else "inconclusive")
                report["cases"][case] = {"status": status, "source_sha256": digest(original),
                                          "attempts": attempts}
                print(f"{case:24} {status:12} {attempts[-1]['ratio']:.3f}x", flush=True)
        # Reject runs whose executables or shared runtime changed mid-measurement.
        if (runtime_identity(args.baseline) != report["baseline"] or
                runtime_identity(args.candidate) != report["candidate"]):
            raise ValueError("runtime changed during measurement; finish builds before benchmarking")
        states = [case["status"] for case in report["cases"].values()]
        report["status"] = ("regression" if "regression" in states else
                            "inconclusive" if "inconclusive" in states else "pass")
        code = {"pass": 0, "regression": 1, "inconclusive": 2}[report["status"]]
    except (OSError, ValueError, subprocess.TimeoutExpired) as error:
        report["error"] = str(error)
        print(f"Performance check invalid: {error}", file=sys.stderr)
    finally:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Performance check: {report['status']}. Report: {args.output}")
    return code


if __name__ == "__main__":
    sys.exit(main())
