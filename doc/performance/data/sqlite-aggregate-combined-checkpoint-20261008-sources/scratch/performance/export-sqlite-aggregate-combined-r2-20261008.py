"""R2 export for combined-controller SQLite evidence; original exporter remains frozen.
No runtimes, builds, git or recursion.

This is prepared before candidate validation finishes. Do not execute until root
selects a terminal --validation record. Default destination is a scratch preview.
Scoring requires validated correctness/gate plus a complete original sqlite_synth
record tied to that controller. Failed stages remain visible and unscored.
"""
from __future__ import annotations
import argparse
import csv
import hashlib
import html
import io
import json
import math
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / "doc/performance/data"
BASE = ROOT / "scratch/performance"
PREFIX = "sqlite-aggregate-combined-checkpoint-20261008"
BENCHMARK = "sqlite_synth"
CP_PREFIX = "pyperformance-cpython3147-live-eval-full-fast-20261007"
CP_BINARY_SHA = "4942b86a6597e5aee0128daa00050ed79bc21f6e709a78eb19cbfeb0c2f39ac9"
inventory = {}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def remember(path, archive=True, expected=None):
    path = Path(path).resolve()
    require(path.is_file(), "Missing evidence: " + str(path))
    raw = path.read_bytes()
    require(expected is None or sha(raw) == expected, "Evidence hash mismatch: " + str(path))
    if path in inventory:
        inventory[path]["archive"] |= archive
    else:
        inventory[path] = {"raw": raw, "sha256": sha(raw), "bytes": len(raw), "archive": archive}
    return raw


def record(path, archive=True):
    return json.loads(remember(path, archive).decode("utf-8-sig"))


def phases(row):
    result = []
    for phase in row.get("phases", []):
        if "stdout_log" in phase:
            stdout = remember(DATA / phase["stdout_log"], expected=phase["stdout_sha256"])
            stderr = remember(DATA / phase["stderr_log"], expected=phase["stderr_sha256"])
            # Report decoding is separate from the byte-exact archived streams.
            text = stdout.decode("utf-8", errors="replace") + "\n" + stderr.decode("utf-8", errors="replace")
            result.append({**phase, "text": text, "log": phase["stdout_log"], "stderr_log": phase["stderr_log"]})
        else:
            path = DATA / phase["log"]
            raw = remember(path, expected=phase["sha256"])
            result.append({**phase, "text": raw.decode("utf-8", errors="replace")})
    return result


def values(suite, runtime, path):
    matches = [b for b in suite["benchmarks"] if b.get("metadata", {}).get("name", suite.get("metadata", {}).get("name")) == BENCHMARK]
    require(len(matches) == 1, "Missing/duplicate original sqlite_synth: " + str(path))
    rows = []
    for run_number, run in enumerate(matches[0]["runs"], 1):
        for sample_number, seconds in enumerate(run.get("values", []), 1):
            require(isinstance(seconds, (int, float)) and math.isfinite(seconds) and seconds > 0,
                    "Invalid official timing value")
            rows.append({"benchmark": BENCHMARK, "runtime": runtime, "run": run_number,
                         "sample": sample_number, "seconds": seconds, "source": str(path.name)})
    require(len(rows) == 20, "Keep all 20 fast-mode official values: " + runtime)
    return rows


def csv_raw(fields, rows):
    buffer = io.StringIO(newline="")
    writer = csv.DictWriter(buffer, fields)
    writer.writeheader()
    writer.writerows(rows)
    return buffer.getvalue().encode()


def write_exact(path, raw):
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        require(path.read_bytes() == raw, "Refusing to replace existing export: " + str(path))
    else:
        with path.open("xb") as stream:
            stream.write(raw)


def chart(rows):
    maximum = max(1.0, *(r["speed_vs_cpython"] for r in rows))
    scale = 590 / (maximum * 1.15)
    chunks = ['<svg xmlns="http://www.w3.org/2000/svg" width="980" height="240" viewBox="0 0 980 240">',
              '<rect width="980" height="240" fill="#ffffff"/>',
              '<text x="24" y="32" font-family="Arial" font-size="20">Original sqlite_synth — CPython 3.14.7 = 1×</text>',
              '<text x="24" y="56" font-family="Arial" font-size="13">Speed = CPython mean time / runtime mean time. Higher is faster.</text>']
    for number, row in enumerate(rows):
        y = 86 + number * 59
        width = row["speed_vs_cpython"] * scale
        label = "CPython 3.14.7" if row["runtime"] == "cpython3147" else "XLang3 candidate"
        chunks += [f'<text x="24" y="{y + 22}" font-family="Arial" font-size="15">{label}</text>',
                   f'<rect x="205" y="{y}" width="{width:.3f}" height="32" fill="{"#64748b" if number == 0 else "#2563eb"}"/>',
                   f'<text x="{220 + width:.3f}" y="{y + 22}" font-family="Arial" font-size="15">{row["speed_vs_cpython"]:.3f}×</text>']
    chunks += ['<text x="24" y="220" font-family="Arial" font-size="12">20 measured values each; warmups/calibrations remain in original JSON. See report for warnings.</text>', '</svg>']
    return "\n".join(chunks).encode()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--validation", type=Path,
        default=DATA / "sqlite-identity-hash-combined-validation-20261008.json")
    parser.add_argument("--output-dir", type=Path, default=BASE / (PREFIX + "-preview"))
    parser.add_argument("--extra-record", type=Path, action="append", default=[],
        help="Explicit later owner/guard diagnostic JSON to preserve; never a glob.")
    parser.add_argument("--extra-raw", type=Path, action="append", default=[],
        help="Explicit additional raw fixture/log/provenance input; never a glob.")
    args = parser.parse_args()
    destination = args.output_dir.resolve()
    archive = destination / "data" / (PREFIX + "-sources")
    final = record(args.validation)
    require(final.get("terminal") and final.get("hashes_unchanged"),
            "Selected validation must be terminal and hash-stable")
    require(final.get("status") != "running", "Refusing live validation")
    final_phases = phases(final)
    if final.get("source_inventory"):
        remember(final["source_inventory"], expected=final["source_inventory_sha256"])
    require(final.get("source_inventory") and len(final.get("source_sha256", {})) >= 31,
            "Supply the complete combined applied source inventory (31 original targets plus any later correction)")
    for path, expected in final.get("source_sha256", {}).items():
        remember(ROOT / path, expected=expected)
    for path, expected in final.get("binaries_sha256", {}).items():
        # Hash the current native identities but do not repack executable/DLL bytes.
        remember(ROOT / path, archive=False, expected=expected)
    if final.get("accepted_control_manifest_sha256"):
        remember(ROOT / "build-repro/controls/inherited-slot-proof-checkpoint-20261008/preserved-release-provenance.json",
                 expected=final["accepted_control_manifest_sha256"])
    r5 = record(DATA / "sqlite-aggregate-r5-validation-20261008.json")
    require(r5.get("terminal") and r5.get("hashes_unchanged"), "Missing frozen R5 failure receipt")
    r5_phases = phases(r5)
    for name in ("sqlite-aggregate-cursor-observe-cpython3147-20261008.log",
                 "sqlite-aggregate-cursor-observe-xlang3-20261008.log"):
        remember(DATA / name)
    cp8 = record(DATA / "sqlite-cursor-r6-cpython3147-20261008.json")
    require(cp8["exit_code"] == 0 and cp8["groups"] == 8 and cp8["output_matches_expected"],
            "Keep the passed eight-group CPython reference")
    remember(DATA / "sqlite-cursor-r6-cpython3147-20261008.log", expected=cp8["log_sha256"])
    combined = record(DATA / "sqlite-cursor-writer-guard-cpython3147-20261008.json")
    require(combined["exit_code"] & 0xffffffff == 0xc0000005, "Preserve combined reference access violation")
    remember(DATA / "sqlite-cursor-writer-guard-cpython3147-20261008.log", expected=combined["log_sha256"])
    remember(BASE / "sqlite-cursor-writer-guard-probe-20261008.py", expected=combined["source_sha256"])
    extra_records = []
    historical_records = [DATA / name for name in (
        "sqlite-aggregate-cursor-r6-validation-20261008.json",
        "sqlite-identity-hash-combined-validation-20261008.json",
        "sqlite-cursor-r6-extra-focused-20261008.json",
        "sqlite-cursor-writer-guard-isolated-cpython3147-20261008.json")]
    seen_records = {args.validation.resolve(), (DATA / "sqlite-aggregate-r5-validation-20261008.json").resolve()}
    for path in [*historical_records, *args.extra_record]:
        path = path.resolve()
        if path in seen_records:
            continue
        seen_records.add(path)
        extra = record(path)
        require(extra.get("terminal", True) and extra.get("status") != "running", "Extra diagnostic is live")
        extra_records.append((path.name, extra, phases(extra)))
        if path.name == "sqlite-cursor-r6-extra-focused-20261008.json":
            remember(DATA / "sqlite-cursor-r6-extra-focused-before-count-repair-20261008.json",
                     expected=extra["prior_record_sha256"])
        for case in extra.get("cases", []):
            for kind in ("stdout", "stderr"):
                if kind + "_file" in case:
                    remember(DATA / case[kind + "_file"], expected=case[kind + "_sha256"])
            if case.get("stdout_file", "").endswith(".stdout.log"):
                individual = DATA / case["stdout_file"].replace(".stdout.log", ".json")
                if individual.is_file():
                    remember(individual)
    for path in args.extra_raw:
        remember(path)
    # A standalone pre-gate case is retained as supplemental evidence only.
    # It cannot supply the accepted controller's score or bypass correctness.
    supplemental_prefix = "sqlite-r6-identity-r2-official-sqlite-synth-20261008"
    supplemental = record(DATA / (supplemental_prefix + "-receipt.json"))
    require(supplemental.get("terminal") and supplemental.get("hashes_unchanged") and
            supplemental.get("exit_code") == 0 and supplemental.get("acceptance", "").startswith("unaccepted"),
            "Preserve the standalone pre-gate case as unaccepted terminal evidence")
    remember(DATA / (supplemental_prefix + ".json"), expected=supplemental["output_sha256"])
    remember(DATA / (supplemental_prefix + ".log"), expected=supplemental["log_sha256"])
    # Deliberately narrow explicit inventory: no prior archives or worktree recursion.
    for stem in ("sqlite-aggregate-r5-proposed-20261008", "sqlite-cursor-r6-proposed-20261008",
                 "sqlite-cursor-r6-v2-proposed-20261008", "sqlite-cursor-r6-v3-proposed-20261008"):
        remember(BASE / (stem + ".patch"))
        remember(BASE / (stem + "-provenance.json"))
    for name in ("sqlite-aggregate-r5-review-20261008.md", "sqlite-cursor-r6-v3-review-20261008.md",
                 "sqlite-cursor-completion-fixture-r6-20261008.py", Path(__file__).name):
        remember(BASE / name)

    samples, summary = [], []
    official = final.get("official_sqlite_synth")
    warning_lines = []
    scored = bool(final.get("status") == "validated" and final.get("correctness_passed") and official and official.get("exit_code") == 0 and official.get("complete"))
    if scored:
        gate_meta = final["fixed_gate"]
        gate = record(DATA / gate_meta["output"])
        require(sha(inventory[(DATA / gate_meta["output"]).resolve()]["raw"]) == gate_meta["sha256"], "Gate receipt mismatch")
        require(gate["status"] == "pass" and len(gate["cases"]) == 11 and
                (gate["repeats"], gate["warmup"], gate["threshold"]) == (21, 5, .1), "Fixed gate changed")
        candidate_path = DATA / official["output"]
        candidate_raw = remember(candidate_path, expected=official["sha256"])
        candidate_suite = json.loads(candidate_raw.decode("utf-8-sig"))
        cp_path = DATA / (CP_PREFIX + ".json")
        cp_suite = record(cp_path)
        cp_provenance = record(DATA / (CP_PREFIX + "-provenance.json"))
        require(cp_provenance["status"] == "finished" and cp_provenance["runtime_version"] == "3.14.7" and
                cp_provenance["sha256_start"]["exe"] == final["cpython3147_binary_sha256"] == CP_BINARY_SHA,
                "CPython reference identity mismatch")
        source_hashes = {p.replace("\\", "/"): value for p, value in cp_provenance["benchmark_python_sources"].items()}
        benchmark_sha = final["benchmark_source_sha256"]
        require(isinstance(benchmark_sha, dict) and "sqlite_synth" in benchmark_sha,
                "Expected combined controller benchmark hash map")
        require(source_hashes["bm_sqlite_synth/run_benchmark.py"] == benchmark_sha["sqlite_synth"],
                "Official benchmark source differs")
        require(cp_provenance["compatibility_hook_sha256"] == final["compatibility_hook_sha256"],
                "Candidate and saved CPython compatibility hooks differ")
        samples = values(cp_suite, "cpython3147", cp_path) + values(candidate_suite, "xlang3_candidate", candidate_path)
        cp_mean = statistics.mean(row["seconds"] for row in samples if row["runtime"] == "cpython3147")
        for runtime in ("cpython3147", "xlang3_candidate"):
            timings = [row["seconds"] for row in samples if row["runtime"] == runtime]
            mean, sd = statistics.mean(timings), statistics.stdev(timings)
            summary.append({"benchmark": BENCHMARK, "runtime": runtime, "values": len(timings),
                            "mean_seconds": mean, "sample_sd_seconds": sd, "min_seconds": min(timings),
                            "max_seconds": max(timings), "cv_percent": 100 * sd / mean,
                            "speed_vs_cpython": cp_mean / mean})
        for phase in final_phases:
            if phase["name"] == "official-sqlite-synth":
                warning_lines += [line for line in phase["text"].splitlines() if any(word in line.lower()
                    for word in ("warning", "unstable", "standard deviation", "maximum", "not enough samples"))]
    elif official and official.get("output") and (DATA / official["output"]).exists():
        remember(DATA / official["output"], expected=official.get("sha256"))
    separate_sqlglot = final.get("official_sqlglot_parse")
    if separate_sqlglot and separate_sqlglot.get("output"):
        sqlglot_path = DATA / separate_sqlglot["output"]
        if sqlglot_path.is_file():
            remember(sqlglot_path, expected=separate_sqlglot.get("sha256"))

    profiler_refs = []
    for name in ("sqlglot-body-profile-validation-20261008.json", "sqlglot-parse-body-native-export-ranges-20261008.md"):
        path = DATA / name if name.endswith(".json") else BASE / name
        if path.is_file():
            remember(path)
            profiler_refs.append(str(path.relative_to(ROOT)).replace("\\", "/"))
    report = ["# SQLite aggregate/cursor and combined runtime checkpoint", "", f"Validation status: `{final['status']}`. "
              + ("Original sqlite_synth comparison follows below." if scored else "This checkpoint is unscored; any supplemental official case has not passed combined acceptance."), "",
              "Own native `_sqlite3` supplies the aggregate bridge. The `sqlite3` wrapper and aggregate algorithms remain Python.", "",
              "| Evidence | Outcome |", "|---|---|",
              "| R5 focused candidate | First five groups passed; replacement after one fetch failed. Full original seven-group fixture retained. |",
              "| Retained cursor CP/X observation | CP replacement after first row succeeded; R5 XLang3 required exhaustion. This isolates native statement progress from temporary ownership. |",
              "| R6 v1 / v2 | Unbuilt review proposals, preserved; SQL string ownership and writer bypasses corrected in v3. |",
              "| New generic cursor CPython 3.14.7 | All eight groups passed; exact unchanged baseline reused. |",
              "| Combined seven-API CPython probe | Native access violation `0xC0000005`, buffered stdout empty. Preserved as failure evidence, not a parity pass. |", ""]
    report += ["The standalone original sqlite_synth run completed with 20 values before the complete correctness/gate acceptance. Its raw JSON, log and terminal receipt are preserved as unaccepted supplemental evidence; it does not replace the selected controller's fresh official run.", ""]
    report += ["| Selected candidate phase | Exit |", "|---|---:|"]
    report += [f"| {p['name']} | {p['exit_code']} |" for p in final_phases]
    for name, extra, extra_phases in extra_records:
        report += ["", f"Additional terminal evidence `{name}`: `{extra.get('status', 'status not recorded')}`."]
        if extra_phases:
            report += ["", "| Additional phase | Exit | Registered tests reported |", "|---|---:|---:|"]
            report += [f"| {p['name']} | {p['exit_code']} | {p.get('registered_tests_passed', '')} |" for p in extra_phases]
        if extra.get("harness_issue"):
            report += ["", extra["harness_issue"]]
        if extra.get("cases"):
            report += ["", "| Isolated operation | Exit code | Native access violation | Assertions passed |", "|---|---:|---|---|"]
            for case in extra["cases"]:
                report += [f"| {case.get('operation', case.get('case_id'))} | {case.get('exit_code_hex', case.get('exit_code'))} | "
                           f"{case.get('native_access_violation', 'not recorded')} | {case.get('expected_guard_rejection_and_assertions_passed', 'not recorded')} |"]
            report += ["", "Isolated reference completion preserves crashes and differences; it does not establish whole candidate parity."]
    failures = [p for p in final_phases if p["exit_code"] != 0 or p.get("passed") is False or p.get("output_matches_expected") is False]
    for failed in failures:
        report += ["", f"Selected failure excerpt from `{failed['log']}` and `{failed.get('stderr_log', 'combined legacy stream')}` (full raw bytes archived):", "",
                   "```text", *failed["text"].splitlines()[-12:], "```"]
    if final.get("fixture_counts"):
        report += ["", "Controller fixture counts: `" + json.dumps(final["fixture_counts"], sort_keys=True) + "`."]
    if scored:
        report += ["", "CPython 3.14.7 = **1×**. Speed is CPython mean time divided by runtime mean time; higher is faster. "
                   "The saved CP reference and fresh candidate are unpaired official fast runs, not a paired improvement claim.", "",
                   "| Runtime | Mean ± sample SD (ms) | Values | CV | Speed vs CPython |", "|---|---:|---:|---:|---:|"]
        for row in summary:
            report += [f"| {row['runtime']} | {row['mean_seconds']*1000:.6f} ± {row['sample_sd_seconds']*1000:.6f} | "
                       f"{row['values']} | {row['cv_percent']:.2f}% | {row['speed_vs_cpython']:.4f}× |"]
        report += ["", f"![Original SQLite speed comparison](charts/{PREFIX}.svg)", "",
                   "All 20 official values per runtime are in the sample CSV; calibration and warmups remain in original raw JSON."]
        if warning_lines:
            report += ["", "Official log warning lines (full raw log retained):", ""] + ["> " + line for line in warning_lines]
    report += ["", "Completed cursors now copy current rows, advance before return, cache description and detach/finalize SQL. "
               "SQL/script text is owned before callback-capable cleanup. Same-cursor writers are guarded.", "",
               "The lookahead guard is intentionally stricter than CPython's unlock-before-step implementation, whose cached Statement object owns its native storage. "
               "Our representation owns a raw statement; unrestricted reentrant parity is not claimed.", "",
               "SQLite still does not publish callback edges through the existing SDK GC-reference facility. Factory→connection cycles need explicit close. "
               "The pre-existing scalar registration failure double-destroy remains separate. No full DB-API or whole-suite speed win is claimed."]
    if profiler_refs:
        report += ["", "Independent SQLGlot native-body profiling references are unscored and are not sqlite_synth timing evidence:", ""]
        report += [f"- [{Path(p).name}](data/{PREFIX}-sources/{p})" for p in profiler_refs]
    report += ["", f"Raw-source inventory and SHA-256 receipt: [manifest](data/{PREFIX}-manifest.json).", ""]

    report += ["", "The selected combined receipt also covers identity R2 and hash runtime changes. Source inventory and all declared source bytes are archived; stdout and stderr remain distinct. Failure or incomplete official output is unscored. This SQLite page does not turn the separate SQLGlot or String diagnostic into a SQLite speed claim.", ""]
    generated = {destination / (PREFIX + ".md"): "\n".join(report).encode()}
    if scored:
        generated[destination / "data" / (PREFIX + "-samples.csv")] = csv_raw(list(samples[0]), samples)
        generated[destination / "data" / (PREFIX + "-summary.csv")] = csv_raw(list(summary[0]), summary)
        generated[destination / "charts" / (PREFIX + ".svg")] = chart(summary)
    # Freeze every input byte before any output mutation. No destination can be an input.
    for path, info in inventory.items():
        require(archive not in path.parents and path != archive and path not in generated,
                "Export destination was selected as input")
        require(path.read_bytes() == info["raw"], "Input changed during read: " + str(path))
    manifest_rows = []
    for path, info in inventory.items():
        relative = path.relative_to(ROOT)
        archived = "data/" + PREFIX + "-sources/" + relative.as_posix() if info["archive"] else None
        manifest_rows.append({"source": relative.as_posix(), "sha256": info["sha256"], "bytes": info["bytes"], "archived": archived})
        if archived:
            write_exact(destination / archived, info["raw"])
    for path, raw in generated.items():
        write_exact(path, raw)
    receipt = {"status": "saved_terminal_evidence_export", "validation": args.validation.name,
               "validation_status": final["status"], "official_scored": bool(scored),
               "official_samples": len(samples), "source_inventory_count": len(final.get("source_sha256", {})), "inputs": manifest_rows,
               "generated": [{"path": str(p.relative_to(destination)).replace("\\", "/"), "sha256": sha(raw), "bytes": len(raw)}
                             for p, raw in generated.items()]}
    write_exact(destination / "data" / (PREFIX + "-manifest.json"), (json.dumps(receipt, indent=2) + "\n").encode())
    print(json.dumps({"status": receipt["status"], "official_scored": bool(scored), "raw_archived": sum(r["archived"] is not None for r in manifest_rows)}))


if __name__ == "__main__":
    main()
