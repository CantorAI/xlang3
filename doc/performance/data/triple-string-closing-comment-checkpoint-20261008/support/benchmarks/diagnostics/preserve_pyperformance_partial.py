"""Retain failed-definition output before pyperformance removes its temp file.

Preserved samples are partial evidence. They never turn a failed definition
into a completed one or silently join the successful suite population.
"""
import hashlib
import json
from pathlib import Path
import re


def preserve_partial_output(argv, suite_output, benchmark, exit_code):
    if not exit_code or "--output" not in argv:
        return None
    index = argv.index("--output") + 1
    if index >= len(argv):
        return None
    temporary = Path(argv[index])
    if not temporary.is_file():
        return None
    raw = temporary.read_bytes()
    if not raw:
        return None
    digest = hashlib.sha256(raw).hexdigest()
    output = Path(suite_output)
    directory = output.parent / (output.stem + "-partial")
    directory.mkdir(parents=True, exist_ok=True)
    name = re.sub(r"[^A-Za-z0-9._-]", "_", str(benchmark or "unknown"))[:80]
    preserved = directory / f"{name}-{digest[:16]}.json"
    # Store original bytes, including invalid/truncated JSON. Do not reconstruct
    # timings from the rounded means printed to the console.
    if preserved.exists() and preserved.read_bytes() != raw:
        raise ValueError("partial-output identity collision")
    preserved.write_bytes(raw)
    record = {"definition": benchmark, "definition_status": "failed",
              "exit_code": exit_code, "partial_output": preserved.name,
              "sha256": digest, "bytes": len(raw), "json_valid": False}
    try:
        parsed = json.loads(raw)
    except (ValueError, UnicodeError) as error:
        record["parse_error"] = str(error)
    else:
        record["json_valid"] = True
        if isinstance(parsed, dict):
            try:
                record["timed_subtests"] = [
                    {"name": item.get("metadata", {}).get("name",
                             parsed.get("metadata", {}).get("name")),
                     "values": sum(len(run.get("values", [])) for run in item.get("runs", []))}
                    for item in parsed.get("benchmarks", []) if isinstance(item, dict)
                ]
            except (AttributeError, TypeError) as error:
                record["schema_error"] = str(error)
    preserved.with_suffix(".provenance.json").write_text(
        json.dumps(record, indent=2) + "\n", encoding="utf-8", newline="\n")
    return record
