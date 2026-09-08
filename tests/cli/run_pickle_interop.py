# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0

import pathlib
import subprocess
import sys


def checked(command, cwd, expected=None):
    result = subprocess.run(command, cwd=cwd, text=True, capture_output=True)
    actual = result.stdout.replace("\r\n", "\n").strip()
    if result.returncode or (expected is not None and actual != expected):
        raise RuntimeError(
            f"command failed ({result.returncode}): {command!r}\n"
            f"expected={expected!r}\nactual={actual!r}\nstderr={result.stderr}"
        )


def main():
    xlang3, work_dir = sys.argv[1:3]
    work = pathlib.Path(work_dir)
    work.mkdir(parents=True, exist_ok=True)
    root = pathlib.Path(__file__).resolve().parents[2]
    fixtures = root / "tests" / "fixtures" / "compat_sections"
    for name in ("xlang3_pickle_interop.bin", "cpython_pickle_interop.bin"):
        (work / name).unlink(missing_ok=True)

    checked([xlang3, str(fixtures / "pickle_interop_writer.py")], work, "pickle-written")
    checked([
        sys.executable, "-c",
        "import pickle; f=open('xlang3_pickle_interop.bin','rb'); "
        "data=pickle.load(f); f.close(); print(data['name'], data['items'][2], data['flag'])",
    ], work, "xlang3 3 True")
    checked([
        sys.executable, "-c",
        "import pickle; f=open('cpython_pickle_interop.bin','wb'); "
        "pickle.dump({'name':'cpython','items':[1,2,3],'flag':False}, f, 4); f.close()",
    ], work)
    checked([xlang3, str(fixtures / "pickle_interop_reader.py")], work, "cpython 3 False")


if __name__ == "__main__":
    main()
