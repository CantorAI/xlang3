"""Summarize the diagnostic native VM scope timers, optionally removing startup.

Use the IR enum from the profiled revision. Clock probes perturb execution;
these values locate native costs and are not pyperf performance scores. A
shorter run with identical setup can be subtracted from a longer workload run.
Raw negative deltas are preserved because startup and scheduling noise remain.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
ROW = re.compile(
    r"vm-time: opcode=(\d+) calls=(\d+) self_ns=(\d+) total_ns=(\d+)"
)


def identity(path):
    return {"path": str(path.resolve()), "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def read_profile(path):
    text = path.read_text(encoding="utf-8-sig")
    rows = {}
    for match in ROW.finditer(text):
        opcode, calls, self_ns, total_ns = map(int, match.groups())
        if opcode in rows:
            raise ValueError(f"{path}: multiple processes/scopes reported opcode {opcode}")
        rows[opcode] = {"calls": calls, "self_ns": self_ns, "total_ns": total_ns}
    if not rows:
        raise ValueError(f"{path}: no native VM timing rows")
    loops = re.findall(r"direct workload .*?\bloops=(\d+)\b", text)
    clock = re.search(r"vm-time: clock_pair_mean_ns=(\d+)", text)
    return rows, int(loops[0]) if len(loops) == 1 else None, int(clock[1]) if clock else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", type=Path)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--ir", type=Path, default=ROOT / "src/internal/xlang3/ir.h")
    parser.add_argument("--extra-loops", type=int)
    parser.add_argument("--top", type=int, default=20)
    parser.add_argument("--csv", type=Path)
    parser.add_argument("--json", type=Path)
    args = parser.parse_args()

    profile, profile_loops, clock = read_profile(args.profile)
    baseline, baseline_loops, baseline_clock = ({}, 0, None)
    if args.baseline:
        baseline, baseline_loops, baseline_clock = read_profile(args.baseline)
    extra_loops = args.extra_loops
    if extra_loops is None:
        if profile_loops is None or baseline_loops is None:
            raise ValueError("Specify --extra-loops when workload loop counts are ambiguous")
        extra_loops = profile_loops - baseline_loops
    if extra_loops <= 0:
        raise ValueError("The longer run must contain a positive number of extra workload loops")

    ir_text = args.ir.read_text(encoding="utf-8")
    body = ir_text.split("enum class Op : uint16_t {", 1)[1].split("};", 1)[0]
    names = [line.split("//", 1)[0].strip().rstrip(",")
             for line in body.splitlines() if line.split("//", 1)[0].strip()]
    if len(names) > 253:
        raise ValueError("IR opcodes overlap the diagnostic scope IDs")
    scope_names = {253: "VM_INVOCATION", 254: "VM_FRAME_SWITCH", 255: "VM_LOOP_CONTROL"}

    rows = []
    for opcode in sorted(profile.keys() | baseline.keys()):
        after = profile.get(opcode, {"calls": 0, "self_ns": 0, "total_ns": 0})
        before = baseline.get(opcode, {"calls": 0, "self_ns": 0, "total_ns": 0})
        name = (scope_names[opcode] if opcode in scope_names
                else names[opcode] if opcode < len(names) else f"UNKNOWN_{opcode}")
        row = {"opcode": opcode, "name": name}
        for field in ("calls", "self_ns", "total_ns"):
            row[field + "_profile"] = after[field]
            row[field + "_baseline"] = before[field]
            row[field + "_delta"] = after[field] - before[field]
        row["calls_per_extra_loop"] = row["calls_delta"] / extra_loops
        row["self_ms_per_extra_loop"] = row["self_ns_delta"] / extra_loops / 1e6
        row["ns_per_call"] = (row["self_ns_delta"] / row["calls_delta"]
                              if row["calls_delta"] > 0 else None)
        rows.append(row)

    positive = [row for row in rows if row["calls_delta"] > 0 and row["self_ns_delta"] > 0]
    denominator = sum(row["self_ns_delta"] for row in positive)
    for row in rows:
        row["positive_self_share_pct"] = (100 * row["self_ns_delta"] / denominator
                                          if row in positive and denominator else 0)
    ordered = sorted(positive, key=lambda row: row["self_ns_delta"], reverse=True)
    metadata = {
        "schema": 1, "diagnostic_only": True,
        "profile": identity(args.profile),
        "baseline": identity(args.baseline) if args.baseline else None,
        "ir": identity(args.ir), "extra_loops": extra_loops,
        "clock_pair_mean_ns": clock, "baseline_clock_pair_mean_ns": baseline_clock,
        "share_denominator": "positive self-time deltas with positive call-count deltas",
        "rows": rows,
    }
    print(f"Diagnostic native scopes: extra_loops={extra_loops}, clock_pair_mean_ns={clock}")
    print(f"profile_sha256={metadata['profile']['sha256']}")
    print(f"ir_sha256={metadata['ir']['sha256']}")
    negative = [row["name"] for row in rows if row["self_ns_delta"] < 0 or row["calls_delta"] < 0]
    print("Negative raw deltas retained: " + (", ".join(negative) if negative else "none"))
    print(f"{'Scope':32} {'share':>7} {'calls/loop':>12} {'self ms/loop':>13} {'ns/call':>10}")
    for row in ordered[:args.top]:
        print(f"{row['name']:32} {row['positive_self_share_pct']:6.2f}% "
              f"{row['calls_per_extra_loop']:12.1f} {row['self_ms_per_extra_loop']:13.3f} "
              f"{row['ns_per_call']:10.1f}")
    if args.csv:
        with args.csv.open("w", newline="", encoding="utf-8") as file:
            writer = csv.DictWriter(file, fieldnames=list(rows[0]))
            writer.writeheader()
            writer.writerows(rows)
    if args.json:
        args.json.write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
