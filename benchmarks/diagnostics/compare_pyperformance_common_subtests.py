"""Compare two completed XLang3 runs on their shared CPython-matched subtests.

Completion counts and newly timed cases belong in the full-run report. This
companion keeps population changes out of the before/after speed comparison.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path

from summarize_pyperformance_comparison import benchmark_map, mean, timing


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def completed_means(path):
    data = read_json(path)
    if data.get("metadata", {}).get("unit") != "second":
        raise ValueError(f"expected time measurements in seconds: {path}")
    measurements = {}
    for name, benchmark in benchmark_map(data).items():
        value = mean(benchmark)
        if value is None:
            continue
        if not math.isfinite(value) or value <= 0:
            raise ValueError(f"invalid timing for {name}: {path}")
        count = sum(len(run.get("values", [])) for run in benchmark["runs"])
        measurements[name] = (value, count)
    return measurements


def geomean(values):
    return math.exp(sum(math.log(value) for value in values) / len(values))


def compare(before, after, reference):
    common = sorted(before.keys() & after.keys() & reference.keys())
    if not common:
        raise ValueError("no common completed subtests matched to CPython")
    return [dict(
        subtest=name, before_seconds=before[name][0], after_seconds=after[name][0],
        cpython_seconds=reference[name][0], before_values=before[name][1],
        after_values=after[name][1], cpython_values=reference[name][1],
        before_cpython_speed=reference[name][0] / before[name][0],
        after_cpython_speed=reference[name][0] / after[name][0],
        before_over_after_speed=before[name][0] / after[name][0],
    ) for name in common]


def validate_completed_provenance(provenance, reference_name):
    if provenance.get("status") not in {"finished", "finished_with_benchmark_failures"}:
        raise ValueError("the after run is not terminal; do not summarize partial results")
    start, end = provenance.get("sha256_start", {}), provenance.get("sha256_end", {})
    # A native package is part of the measured engine too. Older provenance
    # records only EXE/DLL; verify any package identity recorded by newer runs.
    kinds = ("exe", "dll", "hashlib") if start.get("hashlib") else ("exe", "dll")
    for kind in kinds:
        if not start.get(kind) or start[kind].lower() != end.get(kind, "").lower():
            raise ValueError(f"missing or changed after-run {kind} hash")
    # The runners record either `python --version` or platform.python_version().
    # Both must identify exactly the required reference version.
    if provenance.get("manager_version") not in {"Python 3.14.7", "3.14.7"}:
        raise ValueError("the run manager must be CPython 3.14.7")
    if provenance.get("cpython_reference") != reference_name:
        raise ValueError("CPython reference does not match the completed run provenance")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("before-json", "after-json", "cpython-json", "after-provenance", "output-prefix"):
        parser.add_argument(f"--{name}", type=Path, required=True)
    args = parser.parse_args()
    provenance = read_json(args.after_provenance)
    validate_completed_provenance(provenance, args.cpython_json.name)
    before = completed_means(args.before_json)
    after = completed_means(args.after_json)
    reference = completed_means(args.cpython_json)
    rows = compare(before, after, reference)
    common = {row["subtest"] for row in rows}
    stats = {
        "common_subtests": len(rows),
        "before_cpython_speed_geomean": geomean([row["before_cpython_speed"] for row in rows]),
        "after_cpython_speed_geomean": geomean([row["after_cpython_speed"] for row in rows]),
        "before_over_after_speed_geomean": geomean([row["before_over_after_speed"] for row in rows]),
        "before_nominal_cpython_wins": sum(row["before_cpython_speed"] > 1 for row in rows),
        "after_nominal_cpython_wins": sum(row["after_cpython_speed"] > 1 for row in rows),
        "after_cpython_matched_outside_common": sorted((after.keys() & reference.keys()) - common),
        "before_cpython_matched_outside_common": sorted((before.keys() & reference.keys()) - common),
        "inputs": {str(path): hashlib.sha256(path.read_bytes()).hexdigest() for path in
                   (args.before_json, args.after_json, args.cpython_json, args.after_provenance)},
    }
    prefix = args.output_prefix
    prefix.parent.mkdir(parents=True, exist_ok=True)
    with prefix.with_suffix(".csv").open("w", encoding="utf-8-sig", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    prefix.with_suffix(".json").write_text(json.dumps(stats, indent=2) + "\n", encoding="utf-8")

    def link(path):
        return Path(os.path.relpath(path, prefix.parent)).as_posix()
    report = [
        "# XLang3 before/after comparison on the same subtests", "",
        f"This comparison includes **{len(rows)}** subtests with completed measurements in both XLang3 runs and the saved CPython 3.14.7 reference. Newly completed cases and failures do not enter these speed ratios.", "",
        f"On this same set, geometric mean speed relative to CPython is **{stats['before_cpython_speed_geomean']:.5f}× before** and **{stats['after_cpython_speed_geomean']:.5f}× after**. The geometric mean old-XLang3/new-XLang3 ratio is **{stats['before_over_after_speed_geomean']:.5f}×**; above 1× means the new XLang3 run is faster.", "",
        "These are arithmetic means of recorded pyperf measurement values; calibration and warmups are excluded. Fast-mode runs contain stability warnings and are not a paired statistical experiment. Nominal ratios do not prove a significant gain, August-performance restoration, or a win across all 97 definitions. See the separate full-run report for complete statuses and timing-population changes.", "",
        f"Nominal CPython wins on this shared set: {stats['before_nominal_cpython_wins']} before, {stats['after_nominal_cpython_wins']} after.", "",
        f"CPython-matched after results excluded because they lack a matching before result: {', '.join(stats['after_cpython_matched_outside_common']) or 'none'}.", "",
        f"CPython-matched before results excluded because they lack a matching after result: {', '.join(stats['before_cpython_matched_outside_common']) or 'none'}.", "",
        f"Inputs: [before]({link(args.before_json)}), [after]({link(args.after_json)}), [CPython 3.14.7]({link(args.cpython_json)}), [completed-run provenance]({link(args.after_provenance)}). Raw-input hashes and common-set statistics are in [{prefix.name}.json]({prefix.name}.json); all measurement counts are retained in [{prefix.name}.csv]({prefix.name}.csv).", "",
        "| Subtest | CPython 3.14.7 | XLang3 before | XLang3 after | CP/before | CP/after | Before/after |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for row in rows:
        report.append(f"| `{row['subtest']}` | {timing(row['cpython_seconds'])} | {timing(row['before_seconds'])} | {timing(row['after_seconds'])} | {row['before_cpython_speed']:.4f}× | {row['after_cpython_speed']:.4f}× | {row['before_over_after_speed']:.4f}× |")
    prefix.with_suffix(".md").write_text("\n".join(report) + "\n", encoding="utf-8")
    print(json.dumps({key: value for key, value in stats.items() if key != "inputs"}, indent=2))


if __name__ == "__main__":
    main()
