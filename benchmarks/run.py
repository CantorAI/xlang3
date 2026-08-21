# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
import argparse
import platform
import statistics
import subprocess
import sys
import time
from pathlib import Path


def ensure_large_text_data(root):
    data_dir = root / "data"
    data_dir.mkdir(exist_ok=True)
    path = data_dir / "large_text.txt"
    if path.exists() and path.stat().st_size > 512 * 1024:
        return

    phrases = [
        "alpha beta gamma delta, quick brown runtime.",
        "ERROR cache miss while parsing nested string payload.",
        "vectorized text path should avoid needless temporary objects.",
        "beta channel reports ALPHA-compatible module import timing.",
        "request id contains unicode-like ascii fallback markers only.",
    ]
    with path.open("w", encoding="utf-8", newline="\n") as f:
        for i in range(9000):
            phrase = phrases[i % len(phrases)]
            level = "ERROR" if i % 7 == 0 else "INFO"
            user = "alpha-user" if i % 5 == 0 else "beta-user"
            f.write(
                f"  {i}|{level}|{user}|{phrase} value={i % 97}, bucket={i % 13}.  \n"
            )


def run_once(command, source, cwd):
    start = time.perf_counter()
    completed = subprocess.run(
        [str(command), str(source)],
        cwd=str(cwd),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    elapsed = time.perf_counter() - start
    if completed.returncode != 0:
        raise RuntimeError(
            f"{command} {source.name} failed with exit code {completed.returncode}\n"
            f"stdout:\n{completed.stdout}\n"
            f"stderr:\n{completed.stderr}"
        )
    return elapsed, completed.stdout.strip()


def measure(command, source, warmup, repeats, cwd):
    expected = None
    for _ in range(warmup):
      _, output = run_once(command, source, cwd)
      expected = output if expected is None else expected

    samples = []
    for _ in range(repeats):
        elapsed, output = run_once(command, source, cwd)
        if expected is not None and output != expected:
            raise RuntimeError(f"{source.name} output changed for {command}: {output!r} != {expected!r}")
        samples.append(elapsed)
    return samples, expected


def fmt_ms(seconds):
    return f"{seconds * 1000.0:9.2f}"


def resolve_command(command, root):
    path = Path(command)
    if path.is_absolute():
        return path
    cwd_path = (Path.cwd() / path).resolve()
    if cwd_path.exists():
        return cwd_path
    root_path = (root / path).resolve()
    if root_path.exists():
        return root_path
    return command


def main():
    parser = argparse.ArgumentParser(description="Run XLang3 vs CPython benchmarks.")
    parser.add_argument("--xlang3", default="../build/Release/xlang3.exe")
    parser.add_argument("--python", default=sys.executable)
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--repeats", type=int, default=7)
    args = parser.parse_args()

    root = Path(__file__).resolve().parent
    repo_root = root.parent
    ensure_large_text_data(root)
    cases = sorted((root / "cases").glob("*.py"))
    if not cases:
        cases = [root / "scalar_loop.py"]

    xlang3 = resolve_command(args.xlang3, root)
    python = resolve_command(args.python, root)

    print(f"Host: {platform.platform()}")
    print(f"Python: {subprocess.check_output([str(python), '--version'], text=True).strip()}")
    print(f"XLang3: {xlang3}")
    print(f"Warmup: {args.warmup}, repeats: {args.repeats}")
    print("")
    print(f"{'case':28} {'python ms':>10} {'xlang3 ms':>10} {'xlang3/python':>14}")
    print("-" * 67)

    for case in cases:
        py_samples, py_output = measure(python, case, args.warmup, args.repeats, repo_root)
        x3_samples, x3_output = measure(xlang3, case, args.warmup, args.repeats, repo_root)
        if py_output != x3_output:
            raise RuntimeError(f"{case.name} output mismatch:\npython={py_output!r}\nxlang3={x3_output!r}")
        py_best = min(py_samples)
        x3_best = min(x3_samples)
        ratio = x3_best / py_best if py_best > 0 else float("inf")
        print(f"{case.stem:28} {fmt_ms(py_best)} {fmt_ms(x3_best)} {ratio:14.2f}")

    print("")
    print("Best-of timings include process startup. Use larger loop counts to reduce startup noise.")


if __name__ == "__main__":
    main()
