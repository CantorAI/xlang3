"""Regression checks for the pyperf compatibility startup hook."""
from __future__ import annotations

import os
from pathlib import Path
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[3]
HOOK = ROOT / "benchmarks" / "diagnostics" / "pyperf_compat" / "sitecustomize.py"


class PyperfCompatStartupTests(unittest.TestCase):
    def run_child(self, code: str) -> subprocess.CompletedProcess[str]:
        env = os.environ.copy()
        existing = env.get("PYTHONPATH")
        paths = [str(HOOK.parent)]
        if existing:
            paths.append(existing)
        env["PYTHONPATH"] = os.pathsep.join(paths)
        return subprocess.run(
            [sys.executable, "-c", code], env=env,
            check=True, capture_output=True, text=True,
        )

    def test_startup_command_does_not_import_pyperf(self) -> None:
        self.run_child(
            "import sys; assert 'pyperf._runner' not in sys.modules; "
            "assert 'pyperf._worker' not in sys.modules"
        )

    def test_benchmark_script_still_installs_worker_compatibility(self) -> None:
        hook = str(HOOK).replace("\\", "\\\\")
        self.run_child(
            "import runpy, sys; sys.argv = ['run_benchmark.py']; "
            f"runpy.run_path(r'{hook}'); "
            "import pyperf._runner as runner; "
            "assert runner.Runner._process_priority(None) is None"
        )


if __name__ == "__main__":
    unittest.main()
