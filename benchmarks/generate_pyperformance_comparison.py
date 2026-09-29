"""Build full-suite pyperformance status, subtest CSVs, and a ratio chart."""
import argparse
import csv
import html
import json
import math
from pathlib import Path
import re
import statistics


CASE_RE = re.compile(r"^\[\s*(\d+)/97\]\s+([a-zA-Z0-9_]+)\.\.\.")
ERROR_RE = re.compile(r"ERROR: Benchmark ([a-zA-Z0-9_]+) (timed out|failed: .+)$")
MEAN_RE = re.compile(
    r"^\s*([a-zA-Z0-9_]+): Mean \+- std dev: "
    r"([0-9.eE+-]+) (ns|us|ms|sec|s) \+-"
)
SECONDS = {"ns": 1e-9, "us": 1e-6, "ms": 1e-3, "sec": 1.0, "s": 1.0}


def pyperf_means(path):
    data = json.loads(path.read_text(encoding="utf-8"))
    result = {}
    for benchmark in data["benchmarks"]:
        name = benchmark["metadata"]["name"]
        values = [value for run in benchmark["runs"] for value in run.get("values", [])]
        if values:
            result[name] = statistics.mean(values)
    return result


def parse_log(path):
    cases = []
    failures = {}
    measurements = {}
    current = None
    in_summary = False
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("Performance version:"):
            in_summary = True
        if in_summary:
            continue
        match = CASE_RE.match(line)
        if match:
            current = match.group(2)
            cases.append(current)
            continue
        match = ERROR_RE.search(line)
        if match:
            reason = match.group(2)
            failures[match.group(1)] = reason.removeprefix("failed: ")
            continue
        match = MEAN_RE.match(line)
        if match and current:
            name, number, unit = match.groups()
            measurements[name] = (float(number) * SECONDS[unit], current)
    if len(cases) != 97:
        raise ValueError(f"expected all 97 benchmark definitions, found {len(cases)}")
    return cases, failures, measurements


def parent_for(subtest, cases):
    return max((name for name in cases
                if subtest == name or subtest.startswith(name + "_")),
               key=len, default="")


def ratio_chart(measurements, cases, output):
    matched = sorted(
        ((name, cp / xl) for name, (cp, xl) in measurements.items()
         if cp > 0 and xl > 0),
        key=lambda item: item[1],
    )
    width = 1400
    left = 350
    right = 1300
    y0 = 86
    row_height = 23
    height = y0 + row_height * len(matched) + 55
    log_min, log_max = -3.0, 2.0

    def xpos(ratio):
        log_ratio = max(log_min, min(log_max, __import__("math").log10(ratio)))
        return left + (log_ratio - log_min) / (log_max - log_min) * (right - left)

    center = xpos(1.0)
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<style>text{font-family:Segoe UI,Arial,sans-serif;fill:#17212b}.title{font-size:23px;font-weight:700}.sub{font-size:14px;fill:#475569}.label{font-size:13px}.value{font-size:12px;fill:#334155}.axis{font-size:12px;fill:#475569}</style>',
        '<rect width="100%" height="100%" fill="#fff"/>',
        '<text x="24" y="30" class="title">XLang3 vs CPython 3.14 — full pyperformance fast results</text>',
        '<text x="24" y="54" class="sub">CPython time ÷ XLang3 time · right of 1× favors XLang3 · log scale · full 97-definition run</text>',
    ]
    for exponent in range(-3, 3):
        x = xpos(10.0 ** exponent)
        stroke = "#2563eb" if exponent == 0 else "#dbe2ea"
        sw = 2 if exponent == 0 else 1
        parts.append(f'<line x1="{x:.1f}" y1="68" x2="{x:.1f}" y2="{y0 + len(matched) * row_height}" stroke="{stroke}" stroke-width="{sw}"/>')
        parts.append(f'<text x="{x:.1f}" y="{height - 14}" text-anchor="middle" class="axis">{10.0 ** exponent:g}×</text>')
    for index, (name, ratio) in enumerate(matched):
        y = y0 + index * row_height
        x = xpos(ratio)
        parts.append(f'<text x="24" y="{y + 11}" class="label">{html.escape(name)}</text>')
        if ratio >= 1:
            bx, bw, color = center, max(1.0, x - center), "#238636"
        else:
            bx, bw, color = x, max(1.0, center - x), "#c2413b"
        parts.append(f'<rect x="{bx:.1f}" y="{y}" width="{bw:.1f}" height="15" rx="3" fill="{color}"/>')
        anchor = "start" if ratio >= 1 else "end"
        label_x = min(right - 2, x + 7) if ratio >= 1 else max(left + 2, x - 7)
        parts.append(f'<text x="{label_x:.1f}" y="{y + 11}" text-anchor="{anchor}" class="value">{ratio:.4g}×</text>')
    parts.append("</svg>")
    output.write_text("\n".join(parts) + "\n", encoding="utf-8")
    return len(matched)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cpython", required=True, type=Path)
    parser.add_argument("--xlang", required=True, type=Path)
    parser.add_argument("--xlang-log", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--stem", required=True)
    parser.add_argument("--chart", required=True, type=Path)
    args = parser.parse_args()

    cases, failures, log_measurements = parse_log(args.xlang_log)
    cp = pyperf_means(args.cpython)
    # Keep raw pyperf samples when the whole definition completed. If a later
    # subtest timed out, pyperformance can discard earlier successful subtests
    # from its JSON file; retain their printed pyperf means from the log.
    xl_sources = {name: "benchmark log (partial definition)"
                  for name in log_measurements}
    xl = {name: value for name, (value, _parent) in log_measurements.items()}
    raw_xl = pyperf_means(args.xlang)
    xl.update(raw_xl)
    xl_sources.update({name: "pyperf JSON" for name in raw_xl})
    cp_parent = {name: parent_for(name, cases) for name in cp}
    xl_parent = {name: parent_for(name, cases) or log_measurements.get(name, (0, ""))[1]
                 for name in xl}
    args.output_dir.mkdir(parents=True, exist_ok=True)

    status_csv = args.output_dir / f"{args.stem}-all-97-status.csv"
    with status_csv.open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.writer(stream)
        writer.writerow(["benchmark", "CPython 3.14 status", "CPython subtests",
                         "XLang3 status", "XLang3 subtests", "failure detail"])
        for case in cases:
            cp_names = sorted(name for name, parent in cp_parent.items() if parent == case)
            xl_names = sorted(name for name, parent in xl_parent.items() if parent == case)
            cp_parts = [f"{name}={cp[name] * 1000:.6g}ms" for name in cp_names]
            xl_parts = [f"{name}={xl[name] * 1000:.6g}ms ({cp[name] / xl[name]:.5g}× CP/XLang)"
                        for name in xl_names]
            if xl_parts and case in failures:
                status = "partial: " + failures[case]
            elif xl_parts:
                status = "completed"
            else:
                status = "failed: " + failures.get(case, "no matching measurement")
            writer.writerow([case, "completed" if cp_parts else "no CPython timing",
                             "; ".join(cp_parts), status, "; ".join(xl_parts),
                             failures.get(case, "")])

    subtests_csv = args.output_dir / f"{args.stem}-subtests.csv"
    all_names = sorted(set(cp) | set(xl))
    with subtests_csv.open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.writer(stream)
        writer.writerow(["subtest", "benchmark", "CPython 3.14 seconds", "XLang3 seconds",
                         "speedup CPython/XLang3 (>1 means XLang3 faster)", "XLang3 status",
                         "XLang3 evidence"])
        for name in all_names:
            parent = cp_parent.get(name) or xl_parent.get(name, "")
            status = ("completed" if name in xl else
                      "failed: " + failures.get(parent, "no matching measurement"))
            writer.writerow([name, parent,
                             "" if name not in cp else f"{cp[name]:.12g}",
                             "" if name not in xl else f"{xl[name]:.12g}",
                             "" if name not in cp or name not in xl else f"{cp[name] / xl[name]:.8g}",
                             status, xl_sources.get(name, "")])

    common = set(cp) & set(xl)
    measurements = {name: (cp[name], xl[name]) for name in common}
    matched = ratio_chart(measurements, cases, args.chart)
    faster = sum(cp[name] > xl[name] for name in common)
    slower = len(common) - faster
    geo_mean = math.exp(statistics.mean(math.log(cp[name] / xl[name]) for name in common))
    print(f"attempted={len(cases)} measured_xlang_subtests={len(xl)} matched={matched} faster={faster} slower={slower} geometric_speedup={geo_mean:.5g}x")
    print(f"status_csv={status_csv}")
    print(f"subtests_csv={subtests_csv}")
    print(f"chart={args.chart}")


if __name__ == "__main__":
    main()
