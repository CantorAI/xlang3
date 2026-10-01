"""Decode numeric opcode rows from ``xlang3 --perf-counters`` output."""
import argparse
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("report", type=Path)
    parser.add_argument("--top", type=int, default=25)
    args = parser.parse_args()

    ir = (ROOT / "src/internal/xlang3/ir.h").read_text(encoding="utf-8")
    body = ir.split("enum class Op : uint16_t {", 1)[1].split("};", 1)[0]
    # Enum comments describe fused opcodes but are not entries; strip them so
    # the printed counter ids stay aligned with the runtime's numeric op ids.
    op_names = []
    for line in body.splitlines():
        entry = line.split("//", 1)[0].strip().rstrip(",")
        if entry:
            op_names.append(entry)
    counts = {}
    for line in args.report.read_text(encoding="utf-8-sig").splitlines():
        fields = line.split()
        if len(fields) == 4 and fields[:2] == ["perf:", "opcode"]:
            index, count = int(fields[2]), int(fields[3])
            counts[op_names[index]] = count

    for name, count in sorted(counts.items(), key=lambda pair: pair[1], reverse=True)[:args.top]:
        print(f"{name:34} {count}")


if __name__ == "__main__":
    main()
