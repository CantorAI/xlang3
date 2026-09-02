# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.

import os
import subprocess
import sys

cwd = "xlang3_subprocess_cwd_probe"
os.makedirs(cwd, exist_ok=True)
try:
    print("source", subprocess.__file__.endswith("subprocess.py"))
    py = subprocess.run([sys.executable, "-c", "print('child-xlang')"], capture_output=True, text=True)
    print("sys-exec", py.returncode, py.stdout.strip())
    cwd_result = subprocess.run(["cmd", "/c", "cd"], cwd=cwd, capture_output=True, text=True)
    print("cwd", cwd_result.returncode, cwd_result.stdout.strip().replace('\\', '/').endswith(cwd))
    devnull_result = subprocess.run(["cmd", "/c", "echo hidden"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    print("devnull", devnull_result.returncode)
    try:
        subprocess.run(["cmd", "/c", "ping -n 3 127.0.0.1 >nul"], shell=True, timeout=0.01)
    except subprocess.TimeoutExpired as err:
        print("timeout", err.cmd is not None, err.timeout == 0.01)
finally:
    os.rmdir(cwd)
