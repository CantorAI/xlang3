"""Render the retained focused set-growth samples; no benchmark runs here."""

import argparse
import csv
import json
from pathlib import Path


def load(path):
    return {row["size"]: row["median_seconds"]
            for row in json.loads(path.read_text(encoding="utf-8-sig"))["records"]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("control", "candidate", "cpython"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    control, candidate, cpython = (load(getattr(args, name))
                                  for name in ("control", "candidate", "cpython"))
    if not control.keys() == candidate.keys() == cpython.keys():
        raise ValueError("input sizes differ")
    rows = []
    for size in sorted(cpython):
        for runtime, seconds, color in (
            ("CPython 3.14.7", cpython[size], "#2563eb"),
            ("XLang3 candidate", candidate[size], "#d97706"),
            ("XLang3 previous", control[size], "#64748b"),
        ):
            if seconds <= 0:
                raise ValueError("non-positive timing")
            rows.append((size, runtime, seconds, cpython[size] / seconds, color))
    with args.output.with_suffix(".csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.writer(stream)
        writer.writerow(("entries", "runtime", "median_seconds", "speed_cpython_1x"))
        writer.writerows(row[:4] for row in rows)

    left, width, top, row_height = 290, 520, 110, 36
    height = top + row_height * len(rows) + 75
    pieces = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="1110" height="{height}" viewBox="0 0 1110 {height}">',
        '<rect width="100%" height="100%" fill="white"/>',
        '<style>text{font-family:Arial,sans-serif;fill:#172033;font-size:14px}.title{font-size:23px;font-weight:bold}.note{font-size:13px;fill:#4b5563}</style>',
        '<text x="25" y="35" class="title">Growing sets: focused speed comparison</text>',
        '<text x="25" y="60" class="note">CPython 3.14.7 = 1×; longer bars are faster. Median of five samples; linear scale.</text>',
        '<text x="25" y="81" class="note">Membership followed by unique insertion. These are not official pyperformance or whole-suite results.</text>',
    ]
    for tick in (0, 0.25, 0.5, 0.75, 1):
        x = left + width * tick
        pieces.extend((
            f'<line x1="{x}" x2="{x}" y1="{top-10}" y2="{top+row_height*len(rows)}" stroke="#e5e7eb"/>',
            f'<text x="{x}" y="{height-50}" text-anchor="middle">{tick:g}×</text>',
        ))
    for index, (size, runtime, seconds, speed, color) in enumerate(rows):
        y = top + index * row_height
        pieces.extend((
            f'<text x="{left-12}" y="{y+15}" text-anchor="end">{size:,} — {runtime}</text>',
            f'<rect x="{left}" y="{y}" width="{width*speed:.4f}" height="21" fill="{color}"/>',
            f'<text x="{left+width+18}" y="{y+15}">{speed:.5f}× · {seconds*1000:.3f} ms</text>',
        ))
    pieces.extend((
        f'<text x="25" y="{height-17}" class="note">Candidate remains slower than CPython. Previous/candidate gains must not be read as speedups over CPython.</text>',
        '</svg>',
    ))
    args.output.write_text("\n".join(pieces) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
