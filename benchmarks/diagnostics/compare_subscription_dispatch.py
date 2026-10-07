"""Order-balanced XLang3 subscription diagnostic; not a release gate.

Run only after correctness checks, on an idle machine. Every bounded repeat
runs both executable orders. Preserve worker samples and binary identities;
never use these diagnostics instead of the complete fixed regression gate.
"""
import argparse
import hashlib
import json
import math
import platform
import statistics
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path


def identity(executable):
    executable = Path(executable).resolve()
    return {name: hashlib.sha256(path.read_bytes()).hexdigest()
            for name, path in (("exe", executable),
                               ("dll", executable.parent / "xlang3_runtime.dll"))}


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--baseline", required=True)
parser.add_argument("--candidate", required=True)
parser.add_argument("--output", required=True, type=Path)
parser.add_argument("--pairs", type=int, default=7)
parser.add_argument("--operations", type=int, default=100000)
args = parser.parse_args()
if platform.python_version() != "3.14.7":
    parser.error("use the CPython 3.14.7 comparison manager")
if args.output.exists() or args.pairs < 1 or args.operations < 1:
    parser.error("use a fresh output path and positive work counts")
probe = Path(__file__).with_name("native_subscription_dispatch_probe.py").resolve()
executables = [str(Path(args.baseline).resolve()), str(Path(args.candidate).resolve())]
report = {"diagnostic_only": True, "status": "running", "pairs": args.pairs,
          "operations": args.operations, "manager": sys.executable,
          "manager_version": platform.python_version(),
          "probe_sha256": hashlib.sha256(probe.read_bytes()).hexdigest(),
          "started_utc": datetime.now(timezone.utc).isoformat(),
          "executables": executables, "sha256_start": [identity(e) for e in executables],
          "workers": [], "paired_ratios_candidate_over_baseline": {}}


def save():
    args.output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


save()
try:
    for pair in range(args.pairs):
        observations = [[], []]
        for order in ((0, 1), (1, 0)):
            for which in order:
                run = subprocess.run([executables[which], str(probe),
                                      "--operations", str(args.operations), "--repeats", "3"],
                                     capture_output=True, text=True, timeout=120, check=True)
                result = json.loads(run.stdout)
                records = {r["case"]: r for r in result["records"]}
                assert len(records) == 10
                assert all(r["operations"] == args.operations and
                           math.isfinite(r["median_seconds"]) and r["median_seconds"] > 0
                           for r in records.values())
                observations[which].append(records)
                report["workers"].append({"pair": pair, "order": list(order), "runtime": which,
                                          "result": result, "stderr": run.stderr})
        for name in observations[0][0]:
            medians = [statistics.mean(r[name]["median_seconds"] for r in group)
                       for group in observations]
            report["paired_ratios_candidate_over_baseline"].setdefault(name, []).append(medians[1] / medians[0])
        save()
        print("Completed diagnostic pair", pair + 1, "of", args.pairs, flush=True)
    report["sha256_end"] = [identity(e) for e in executables]
    assert report["sha256_start"] == report["sha256_end"], "runtime changed while measuring"
    report["summary"] = {name: {"median_time_ratio": statistics.median(ratios),
                                  "baseline_time_over_candidate": 1 / statistics.median(ratios)}
                         for name, ratios in report["paired_ratios_candidate_over_baseline"].items()}
    report["status"] = "finished"
except Exception as error:
    report["status"] = "invalid"
    report["error"] = repr(error)
    raise
finally:
    report["completed_utc"] = datetime.now(timezone.utc).isoformat()
    save()
