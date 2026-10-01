"""Record a complete reference run without inventing a cross-runtime ratio."""
import argparse
import csv
from pathlib import Path

from generate_pyperformance_comparison import parse_log, parent_for, pyperf_means


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--status", type=Path, required=True)
    parser.add_argument("--timings", type=Path, required=True)
    parser.add_argument("--document", type=Path, required=True)
    args = parser.parse_args()
    cases, failures, printed = parse_log(args.log)
    means = pyperf_means(args.reference)
    measurements = {name: value for name, (value, _case) in printed.items()}
    measurements.update(means)
    parents = {name: printed.get(name, (0, ""))[1] or parent_for(name, cases)
               for name in measurements}
    if any(not case for case in parents.values()):
        raise ValueError("unassigned reference subtest")
    rows = []
    for case in cases:
        names = sorted(name for name, parent in parents.items() if parent == case)
        if case in failures:
            status = "partial" if names else "failed"
        elif names:
            status = "completed"
        else:
            raise ValueError(f"missing completion or failure evidence: {case}")
        times = "; ".join(f"{name}={measurements[name] * 1000:.6g}ms" for name in names)
        rows.append((case, status, times, failures.get(case, "")))
    with args.status.open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.writer(stream)
        writer.writerow(("benchmark", "CPython 3.14 status", "CPython subtests", "failure detail"))
        writer.writerows(rows)
    with args.timings.open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.writer(stream)
        writer.writerow(("subtest", "benchmark", "CPython 3.14 seconds", "evidence"))
        for name in sorted(measurements):
            writer.writerow((name, parents[name], f"{measurements[name]:.12g}",
                             "pyperf JSON" if name in means else "benchmark log (partial definition)"))

    complete = sum(row[1] == "completed" for row in rows)
    partial = sum(row[1] == "partial" for row in rows)
    lines = [
        "# CPython 3.14.7 shared-dependency full reference, 2026-10-01",
        "",
        f"All **97 definitions were attempted**: **{complete} completed**, "
        f"**{len(failures)} failed**, **{partial} partial definitions**, "
        f"and **{len(means)} raw subtest timings**. This is a reference run; "
        "the paired XLang3 run is still in progress. No overall speed comparison is available yet.",
        "",
        "This run uses pyperformance 1.14.0 and pyperf 2.10 in fast mode. "
        "The shared dependency directory is "
        "`venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages`. "
        "Both runtimes use the same priority/host-metadata compatibility hooks, "
        "a 600-second full-definition deadline, and a 120-second `async_tree*` "
        "deadline. Deadlines cover calibration and workers; timed workloads, "
        "loop counts, warmups and sample selection are not rewritten.",
        "",
        "Fast-mode results include pyperf instability warnings in the raw log. "
        "Values below are arithmetic means of raw measurement values, excluding warmups. "
        "A benchmark definition can emit several subtests, so the timing count differs from 97.",
        "",
        "- [Raw pyperf JSON](data/" + args.reference.name + ")",
        "- [Complete log](data/" + args.log.name + ")",
        "- [All 97 statuses](data/" + args.status.name + ")",
        "- [Subtest timings](data/" + args.timings.name + ")",
        "- [Paired binary identities and run manifest](data/pyperformance-native-iocp-shared-deps-run-manifest-20261001.json)",
        "",
        "## Failures remain part of the reference",
        "",
        "`django_template` and `sympy` explicitly fail importing `distutils` "
        "from the shared dependency versions. `2to3`, `python_startup`, and "
        "`python_startup_no_site` fail in pyperf's command timer with exit 1. "
        "Source inspection identifies the original compatibility hook's eager "
        "pyperf import as conflicting with the timer's no-pyperf-import guard; "
        "command-level confirmation is pending. These three failures do not "
        "establish that CPython's timed commands themselves fail.",
        "",
        "The XLang3 binary measured by the companion run is from `1924e4b`; "
        "the new native `_asyncio` source draft is outside that frozen binary. "
        "Do not attribute this reference to the unbuilt candidate.",
        "",
        "## Complete definition list",
        "",
        "| Definition | Reference status | Subtest means | Failure |",
        "|---|---|---|---|",
    ]
    for row in rows:
        lines.append("| " + " | ".join(str(cell).replace("|", "\\|") for cell in row) + " |")
    args.document.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"attempted=97 completed={complete} failed={len(failures)} partial={partial} raw_timings={len(means)}")


if __name__ == "__main__":
    main()
