"""Long paired ASCII follow-up; supplements rather than changes the fixed gate."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--control", required=True, type=Path)
parser.add_argument("--candidate", required=True, type=Path)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
assert sys.version.split()[0] == "3.14.7"
root = Path(__file__).resolve().parents[2]
source = root / "scratch/performance/native-unicode-ascii-paired-input.py"
source.parent.mkdir(parents=True, exist_ok=True)
source.write_text('import json\nimport sys\nimport time\n\n\ndef scalar(text, reads):\n    total = 0\n    for _ in range(reads):\n        total += text[-1] == "a"\n    return total\n\n\ndef length(text, reads):\n    total = 0\n    for _ in range(reads):\n        total += len(text)\n    return total\n\n\ndef tail_slice(text, reads):\n    total = 0\n    for _ in range(reads):\n        total += text[-3:] == "aaa"\n    return total\n\n\ntext = "a" * 65536\noperation = sys.argv[1]\nfunction = {"scalar": scalar, "length": length, "tail_slice": tail_slice}[operation]\nreads = 1000000\nexpected = reads * 65536 if operation == "length" else reads\nassert function(text, 4096) == (4096 * 65536 if operation == "length" else 4096)\nstart = time.perf_counter()\nresult = function(text, reads)\nelapsed = time.perf_counter() - start\nassert result == expected\nprint(json.dumps({"operation": operation, "reads": reads, "checksum": result, "seconds": elapsed}))\n')


def identity(exe):
    exe = exe.resolve()
    return {"exe": str(exe), "sha256": {
        f.name: hashlib.sha256(f.read_bytes()).hexdigest()
        for f in (exe, exe.with_name("xlang3_runtime.dll"))}}


identities = {name: identity(path) for name, path in (("control", args.control), ("candidate", args.candidate))}
rows = []
for operation in ("scalar", "length", "tail_slice"):
    samples = {"control": [], "candidate": []}
    for repeat in range(21):
        order = ("control", "candidate") if repeat % 2 == 0 else ("candidate", "control")
        for name in order:
            env = os.environ.copy()
            env["XLANG3_PYTHON_LIB"] = r"C:\Python\Python314\Lib"
            key = identities[name]["sha256"]["xlang3_runtime.dll"][:16]
            env["PYTHONPYCACHEPREFIX"] = str(root / "scratch/performance/pycache-unicode-ascii" / key)
            result = subprocess.run([identities[name]["exe"], str(source), operation],
                                    cwd=root, env=env, capture_output=True, text=True, timeout=30)
            if result.returncode:
                raise RuntimeError(result.stderr)
            record = json.loads(result.stdout)
            assert record["reads"] == 1000000 and record["operation"] == operation
            samples[name].append(record["seconds"])
    ratios = [b / a for a, b in zip(samples["control"], samples["candidate"])]
    row = {"operation": operation, "reads": 1000000, "pairs": 21,
           "control_seconds": samples["control"], "candidate_seconds": samples["candidate"],
           "candidate_over_control_paired_ratios": ratios,
           "median_candidate_over_control": statistics.median(ratios)}
    rows.append(row)
    print(operation, row["median_candidate_over_control"], flush=True)
for name, path in (("control", args.control), ("candidate", args.candidate)):
    assert identity(path) == identities[name], "binary changed during comparison"
args.output.write_text(json.dumps({"diagnostic_only": True, "manager_version": "3.14.7",
                                  "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                                  "identities": identities, "rows": rows}, indent=2) + "\n")
