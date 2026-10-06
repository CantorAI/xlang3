"""Resolve sampled Windows native instruction pointers through PDB symbols."""

import argparse
import json
import shutil
import subprocess
from collections import Counter
from pathlib import Path


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("samples", type=Path)
    parser.add_argument("--module", default="xlang3_runtime.dll")
    parser.add_argument("--limit", type=int, default=30)
    parser.add_argument(
        "--symbolizer", default=shutil.which("llvm-symbolizer.exe") or
        shutil.which("llvm-symbolizer"),
        help="llvm-symbolizer executable; defaults to one on PATH",
    )
    args = parser.parse_args()
    if not args.symbolizer:
        parser.error("llvm-symbolizer is not on PATH; pass --symbolizer PATH")

    data = json.loads(args.samples.read_text(encoding="utf-8"))
    modules = {module["path"].lower(): module for module in data["modules"]
               if args.module.lower() in module["path"].lower()}
    if not modules:
        parser.error(f"no sampled module matches {args.module!r}")

    for module in modules.values():
        rows = [row for row in data["samples"]
                if row["module"].lower() == module["path"].lower()]
        rows.sort(key=lambda row: row["samples"], reverse=True)
        process = subprocess.run(
            [args.symbolizer, f"--obj={module['path']}", "--relative-address"],
            input="\n".join(row["rva"] for row in rows) + "\n",
            capture_output=True,
            text=True,
            check=True,
        )
        groups = [part.splitlines() for part in process.stdout.strip().split("\n\n")]
        counts = Counter()
        source_counts = Counter()
        addresses = []
        for row, group in zip(rows, groups):
            symbol = group[0] if group else f"{Path(module['path']).name}+{row['rva']}"
            source = group[1] if len(group) > 1 else ""
            counts[symbol] += row["samples"]
            if source and not source.startswith("??:"):
                source_counts[source] += row["samples"]
            addresses.append((row["samples"], row["rva"], symbol, source))

        print(f"module={module['path']} samples={sum(counts.values())}")
        print("Top addresses:")
        for samples, rva, symbol, source in addresses[:args.limit]:
            print(f"{samples:6d} {rva:>12} {symbol}"
                  + (f" ({source})" if source else ""))
        print("Top symbols:")
        for symbol, samples in counts.most_common(args.limit):
            print(f"{samples:6d} {symbol}")
        print("Top source locations:")
        for source, samples in source_counts.most_common(args.limit):
            print(f"{samples:6d} {source}")


if __name__ == "__main__":
    main()
