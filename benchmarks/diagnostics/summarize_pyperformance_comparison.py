"""Summarize a full XLang3 pyperformance run against a saved CPython run.

The status CSV uses a prior all-97 table as the benchmark-definition index;
raw pyperf JSON remains the source of all measured timings.
"""
from __future__ import annotations

import argparse
import csv
import html
import json
import math
import re
import statistics
from collections import Counter, defaultdict
from pathlib import Path


FAILURE_LINE = re.compile(r"(?m)^- (.+?) \((Benchmark (?:timed out|died))\)$")
CASE_LINE = re.compile(r"^\s*\[\s*\d+/\d+\]\s+(.+?)\.\.\.\s*$")
EXCEPTION_LINE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*(?:Error|Exception): .+")


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def benchmark_map(data: dict) -> dict[str, dict]:
    return {item["metadata"]["name"]: item for item in data.get("benchmarks", [])}


def values(benchmark: dict) -> list[float]:
    return [float(value) for run in benchmark.get("runs", [])
            for value in run.get("values", [])]


def mean(benchmark: dict | None) -> float | None:
    samples = values(benchmark) if benchmark else []
    return statistics.fmean(samples) if samples else None


def timing(seconds: float | None) -> str:
    if seconds is None:
        return ""
    if seconds >= 1:
        return f"{seconds * 1000:.4g} ms"
    if seconds >= 0.001:
        return f"{seconds * 1000:.4g} ms"
    if seconds >= 1e-6:
        return f"{seconds * 1e6:.4g} µs"
    return f"{seconds * 1e9:.4g} ns"


def parse_subtests(cell: str) -> list[str]:
    names = []
    for item in cell.split(";"):
        item = item.strip()
        if item and "=" in item:
            names.append(item.split("=", 1)[0].strip())
    return names


def case_sections(log: str) -> dict[str, str]:
    lines = log.splitlines()
    sections: dict[str, list[str]] = {}
    current = None
    for line in lines:
        match = CASE_LINE.match(line)
        if match:
            current = match.group(1)
            sections[current] = []
        elif current is not None:
            sections[current].append(line)
    return {name: "\n".join(section) for name, section in sections.items()}


def failure_details(log: str) -> tuple[dict[str, str], dict[str, str]]:
    failures = {name: reason for name, reason in FAILURE_LINE.findall(log)}
    details = {}
    for case, section in case_sections(log).items():
        if case not in failures:
            continue
        candidates = [line.strip() for line in section.splitlines()
                      if EXCEPTION_LINE.match(line.strip())]
        specific = [line for line in candidates
                    if "Benchmark died" not in line
                    and "failed with exit code" not in line]
        details[case] = specific[0] if specific else failures[case]
    return failures, details


def write_status_csv(path: Path, rows: list[dict]) -> None:
    fields = ["benchmark", "CPython 3.14 status", "CPython subtests",
              "XLang3 status", "XLang3 subtests", "failure detail",
              "CPython failure detail"]
    with path.open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def write_subtests_csv(path: Path, rows: list[dict]) -> None:
    fields = ["benchmark", "subtest", "CPython 3.14 seconds",
              "XLang3 seconds", "CPython / XLang3 speedup", "result"]
    with path.open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def chart_svg(path: Path, comparisons: list[dict], completed: int,
              failed: int, geomean: float) -> None:
    width = 1280
    left = 270
    right = 160
    top = 104
    row_height = 23
    height = top + row_height * len(comparisons) + 66
    plot_width = width - left - right
    minimum, maximum = 0.02, 2.0

    def x(value: float) -> float:
        value = min(max(value, minimum), maximum)
        ratio = (math.log(value) - math.log(minimum)) / (math.log(maximum) - math.log(minimum))
        return left + ratio * plot_width

    pieces = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<style>text{font-family:Arial,sans-serif;fill:#172033}.title{font-size:23px;font-weight:bold}.sub{font-size:13px;fill:#4b5563}.label{font-size:12px}.tick{font-size:11px;fill:#4b5563}.value{font-size:11px;font-weight:bold}.grid{stroke:#e5e7eb;stroke-width:1}.baseline{stroke:#111827;stroke-width:2}.slow{fill:#e58b31}.fast{fill:#169a70}</style>',
        '<rect width="100%" height="100%" fill="white"/>',
        '<text x="28" y="34" class="title">XLang3 vs CPython 3.14.7 — pyperformance fast mode</text>',
        f'<text x="28" y="57" class="sub">{completed}/97 definitions completed; {failed}/97 failed. CPython time ÷ XLang3 time: geomean {geomean:.3f}×. Right of 1× is faster.</text>',
    ]
    ticks = [0.02, 0.05, 0.1, 0.2, 0.5, 1.0, 2.0]
    plot_bottom = top + row_height * len(comparisons)
    for tick in ticks:
        xpos = x(tick)
        cls = "baseline" if tick == 1.0 else "grid"
        pieces.append(f'<line x1="{xpos:.1f}" y1="{top-25}" x2="{xpos:.1f}" y2="{plot_bottom}" class="{cls}"/>')
        pieces.append(f'<text x="{xpos:.1f}" y="{top-33}" text-anchor="middle" class="tick">{tick:g}×</text>')
    for index, row in enumerate(comparisons):
        ypos = top + index * row_height
        ratio = row["speedup"]
        end = x(ratio)
        start = x(minimum)
        color = "fast" if ratio > 1.0 else "slow"
        pieces.append(f'<text x="{left-10}" y="{ypos+12}" text-anchor="end" class="label">{html.escape(row["subtest"])}</text>')
        pieces.append(f'<rect x="{start:.1f}" y="{ypos+2}" width="{max(1.0,end-start):.1f}" height="14" rx="2" class="{color}"/>')
        pieces.append(f'<text x="{width-right+13}" y="{ypos+13}" class="value">{ratio:.2f}×</text>')
    pieces.extend([
        f'<text x="{left}" y="{height-24}" class="sub">Logarithmic scale; 1× means equal time; faster ratios extend right of the vertical 1× line.</text>',
        '</svg>',
    ])
    path.write_text("\n".join(pieces) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--xlang-json", type=Path, required=True)
    parser.add_argument("--cpython-json", type=Path, required=True)
    parser.add_argument("--xlang-log", type=Path, required=True)
    parser.add_argument("--cpython-log", type=Path,
                        help="fresh full-run status log; use it instead of historical CPython statuses")
    parser.add_argument("--canonical-status", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--prefix", required=True)
    parser.add_argument("--release-exe-sha256", required=True)
    parser.add_argument("--release-dll-sha256", required=True)
    parser.add_argument("--case-timeout", type=int, default=120,
                        help="full-case timeout in seconds for this run")
    parser.add_argument("--case-timeout-override", action="append", default=[],
                        help="record an override such as async_tree*=20s")
    parser.add_argument("--xlang-stdlib", default="Python 3.14 standard library")
    args = parser.parse_args()
    if args.case_timeout <= 0:
        parser.error("--case-timeout must be positive")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    xdata = load_json(args.xlang_json)
    cdata = load_json(args.cpython_json)
    xbench = benchmark_map(xdata)
    cbench = benchmark_map(cdata)
    log = args.xlang_log.read_text(encoding="utf-8", errors="replace")
    failures, details = failure_details(log)
    cp_failures, cp_details = {}, {}
    cp_log = None
    if args.cpython_log is not None:
        cp_log = args.cpython_log.read_text(encoding="utf-8", errors="replace")
        cp_failures, cp_details = failure_details(cp_log)

    with args.canonical_status.open(newline="", encoding="utf-8-sig") as stream:
        canonical = list(csv.DictReader(stream))
    case_order = [row["benchmark"] for row in canonical]
    if len(case_order) != 97 or len(set(case_order)) != 97:
        raise ValueError("the full-run index must contain 97 unique benchmark definitions")
    attempted = set(case_sections(log))
    missing = set(case_order) - attempted
    unexpected = attempted - set(case_order)
    if missing or unexpected:
        raise ValueError(
            "the log is not a complete all-97 run: "
            f"missing={sorted(missing)}, unexpected={sorted(unexpected)}")
    if cp_log is not None:
        cp_attempted = set(case_sections(cp_log))
        if cp_attempted != set(case_order):
            raise ValueError("the CPython log is not a complete all-97 run")
    name_to_case: dict[str, str] = {}
    for row in canonical:
        for column in ("CPython subtests", "XLang3 subtests"):
            for name in parse_subtests(row.get(column, "")):
                name_to_case[name] = row["benchmark"]
        name_to_case.setdefault(row["benchmark"], row["benchmark"])

    c_by_case: dict[str, list[tuple[str, float]]] = defaultdict(list)
    x_by_case: dict[str, list[tuple[str, float]]] = defaultdict(list)
    for name, benchmark in cbench.items():
        case = name_to_case.get(name)
        value = mean(benchmark)
        if case and case not in cp_failures and value is not None:
            c_by_case[case].append((name, value))
    for name, benchmark in xbench.items():
        case = name_to_case.get(name)
        value = mean(benchmark)
        if case and case not in failures and value is not None:
            x_by_case[case].append((name, value))

    canonical_by_case = {row["benchmark"]: row for row in canonical}
    subtest_rows = []
    comparisons = []
    status_rows = []
    for case in case_order:
        prior = canonical_by_case[case]
        cp_values = dict(c_by_case.get(case, []))
        x_values = dict(x_by_case.get(case, []))
        cp_text_parts = [f"{name}={timing(value)}" for name, value in c_by_case.get(case, [])]
        x_text_parts = []
        for name, value in x_by_case.get(case, []):
            cp = cp_values.get(name)
            ratio = cp / value if cp is not None and value else None
            result = "faster" if ratio is not None and ratio > 1 else "slower" if ratio is not None else "unmatched"
            x_text_parts.append(f"{name}={timing(value)}" + (f" ({ratio:.4f}× CP/XLang)" if ratio is not None else ""))
            subtest_rows.append({
                "benchmark": case,
                "subtest": name,
                "CPython 3.14 seconds": f"{cp:.12g}" if cp is not None else "",
                "XLang3 seconds": f"{value:.12g}",
                "CPython / XLang3 speedup": f"{ratio:.8g}" if ratio is not None else "",
                "result": result,
            })
            if ratio is not None:
                comparisons.append({"benchmark": case, "subtest": name,
                                    "cpython": cp, "xlang": value,
                                    "speedup": ratio})

        if case in cp_failures:
            cp_status = f"failed: {cp_failures[case]}"
        elif cp_values:
            cp_status = "completed"
        elif cp_log is not None:
            cp_status = "not recorded"
        else:
            cp_status = prior.get("CPython 3.14 status", "not recorded")
        if case in failures:
            # Failed definitions can contain earlier subtest values. Preserve
            # them only in raw evidence, never as chart/aggregate speed scores.
            x_status = f"failed: {failures[case]}"
        elif x_values:
            x_status = "completed"
        else:
            x_status = "not recorded"
        status_rows.append({
            "benchmark": case,
            "CPython 3.14 status": cp_status,
            "CPython subtests": "; ".join(cp_text_parts),
            "XLang3 status": x_status,
            "XLang3 subtests": "; ".join(x_text_parts),
            "failure detail": details.get(case, failures.get(case, "")),
            "CPython failure detail": (cp_details.get(case, "") if cp_log is not None
                                       else prior.get("CPython failure detail", "")),
        })

    comparisons.sort(key=lambda item: item["speedup"])
    unknown_outcomes = [row["benchmark"] for row in status_rows
                        if row["XLang3 status"] == "not recorded"]
    if unknown_outcomes:
        raise ValueError(f"benchmark outcomes are missing: {unknown_outcomes}")
    if cp_log is not None:
        cp_unknown = [row["benchmark"] for row in status_rows
                      if row["CPython 3.14 status"] == "not recorded"]
        if cp_unknown:
            raise ValueError(f"CPython benchmark outcomes are missing: {cp_unknown}")
    matched = len(comparisons)
    faster = sum(1 for item in comparisons if item["speedup"] > 1)
    geomean = math.exp(statistics.fmean(math.log(item["speedup"]) for item in comparisons)) if comparisons else 0.0
    completed = sum(1 for row in status_rows if row["XLang3 status"] == "completed")
    failed = sum(1 for row in status_rows if row["XLang3 status"].startswith("failed:"))

    status_path = args.output_dir / f"{args.prefix}-all-97-status.csv"
    subtest_path = args.output_dir / f"{args.prefix}-subtests.csv"
    write_status_csv(status_path, status_rows)
    write_subtests_csv(subtest_path, subtest_rows)
    chart_path = args.output_dir.parent / f"{args.prefix}.svg"
    chart_svg(chart_path, comparisons, completed, failed, geomean)

    failures_by_reason = Counter(failures.values())
    worst = comparisons[:5]
    wins = sorted((item for item in comparisons if item["speedup"] > 1),
                  key=lambda item: item["speedup"], reverse=True)
    report_path = args.output_dir.parent / f"{args.prefix}.md"
    rel = lambda path: path.relative_to(report_path.parent).as_posix()
    report = [
        f"# XLang3 vs CPython 3.14.7: corrected full pyperformance run",
        "",
        f"The XLang3 run attempted all **{len(status_rows)}** pyperformance 1.14.0 definitions in `--fast` mode. It completed **{completed}** definitions and recorded **{failed}** failures/timeouts. "
        + ("Benchmark failures make the suite unsuccessful; every definition was attempted."
           if failed else "Every definition completed."),
        "",
        f"Of **{matched}** matched subtests, XLang3 was faster on **{faster}**. The geometric mean of CPython time divided by XLang3 time was **{geomean:.5f}×**; values over 1× favor XLang3. Fast-mode samples carry stability warnings and are directional evidence.",
        "",
        f"![Horizontal log-scale speed ratio chart; bars extending right of 1× favor XLang3]({args.prefix}.svg)",
        "",
        "## Run configuration",
        "",
        f"- XLang3 Release executable SHA-256: `{args.release_exe_sha256}`.",
        f"- XLang3 runtime DLL SHA-256: `{args.release_dll_sha256}`.",
        f"- CPython reference: pyperformance 1.14.0 on CPython 3.14.7; XLang3 loaded `{args.xlang_stdlib}`.",
        "- The XLang3 `PYTHONPATH` contains the Windows pyperf compatibility shim and the shared benchmark dependency site-packages. No Python 3.13 standard-library overlay was used.",
        f"- Each XLang3 benchmark definition had a {args.case_timeout}-second cap covering pyperf worker calibration and measurement. Overrides: {', '.join(args.case_timeout_override) if args.case_timeout_override else 'none'}.",
        "",
        "## Largest slowdowns and wins",
        "",
        "| Subtest | CPython 3.14.7 | XLang3 | CPython / XLang3 |",
        "|---|---:|---:|---:|",
    ]
    for item in worst:
        report.append(f"| `{item['subtest']}` | {timing(item['cpython'])} | {timing(item['xlang'])} | {item['speedup']:.3f}× |")
    report.extend(["", "Measured wins:", ""])
    if wins:
        for item in wins:
            report.append(f"- `{item['subtest']}`: **{item['speedup']:.3f}×** ({timing(item['xlang'])} vs {timing(item['cpython'])}).")
    else:
        report.append("- None in this matched set.")
    report.extend([
        "",
        "## Failure breakdown",
        "",
    ])
    for reason, count in sorted(failures_by_reason.items()):
        report.append(f"- {count} definitions: {reason}.")
    report.extend([
        "",
        "The [all-97 status CSV](" + rel(status_path) + ") retains every benchmark definition and failure status. The [matched subtest CSV](" + rel(subtest_path) + ") contains raw per-subtest means and speed ratios.",
        "Partial values from failed definitions remain in raw evidence and are excluded from timing tables, speed ratios and aggregate statistics.",
        "",
        "## Raw evidence",
        "",
        f"- XLang3 pyperf JSON: [`{args.xlang_json.name}`]({rel(args.xlang_json)}).",
        f"- Runner status log: [`{args.xlang_log.name}`]({rel(args.xlang_log)}).",
        f"- CPython 3.14.7 pyperf JSON: [`{args.cpython_json.name}`]({rel(args.cpython_json)}).",
        "- Earlier runs that used a Python 3.13 standard library are retained separately; this run supersedes them as the same-version Python 3.14 comparison.",
        "",
        "Benchmark worker failures have several causes, including unavailable optional benchmark dependencies, XLang3 native-module gaps, and interpreter compatibility bugs. The status CSV gives case-level failure details, and the runner log preserves worker tracebacks when available. A worker death is not a performance score; inspect its case-specific cause before treating it as a speed result.",
    ])
    if args.cpython_log is not None:
        report.extend(["", f"- Fresh CPython status log: [`{args.cpython_log.name}`]({rel(args.cpython_log)})."])
    report_path.write_text("\n".join(report) + "\n", encoding="utf-8")

    print(f"Completed definitions: {completed}/97; failed: {failed}; matched subtests: {matched}; faster: {faster}; geomean CPython/XLang3: {geomean:.5f}x")
    print(f"unmapped XLang3 result names: {sorted(set(xbench) - set(name_to_case))}")
    print(f"wrote {report_path}")
    print(f"wrote {status_path}")
    print(f"wrote {subtest_path}")
    print(f"wrote {chart_path}")


if __name__ == "__main__":
    main()
