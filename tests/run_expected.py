# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

import argparse
import pathlib
import subprocess
import sys


def normalized(text):
    return text.replace("\r\n", "\n").rstrip()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("executable")
    parser.add_argument("source")
    parser.add_argument("expected")
    parser.add_argument("arguments", nargs="*")
    args = parser.parse_args()

    command = [args.executable, args.source, *args.arguments]
    result = subprocess.run(command, text=True, capture_output=True)
    if result.returncode:
        sys.stderr.write(result.stdout)
        sys.stderr.write(result.stderr)
        raise SystemExit(f"fixture failed with exit code {result.returncode}: {args.source}")

    expected = normalized(pathlib.Path(args.expected).read_text(encoding="utf-8"))
    actual = normalized(result.stdout)
    if actual != expected:
        raise SystemExit(
            f"output mismatch for {args.source}\n--- expected ---\n{expected}"
            f"\n--- actual ---\n{actual}\n--- stderr ---\n{result.stderr}"
        )


if __name__ == "__main__":
    main()
