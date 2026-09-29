import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location(
    "gate", Path(__file__).resolve().parents[1] / "check_regression.py")
gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gate)


class RegressionGateTests(unittest.TestCase):
    def test_clear_regression(self):
        self.assertEqual(gate.assess([1.] * 9, [1.3] * 9, .1)["status"], "regression")

    def test_equal_and_faster(self):
        for value in (1., .8):
            self.assertEqual(gate.assess([1.] * 9, [value] * 9, .1)["status"], "pass")

    def test_noisy_threshold_is_inconclusive(self):
        self.assertEqual(gate.assess([1.] * 9, [.9, 1., 1., 1.05, 1.1, 1.2, 1.3, 1.4, 1.5], .1)["status"],
                         "inconclusive")

    def test_single_outlier_does_not_fail(self):
        self.assertEqual(gate.assess([1.] * 9, [1.] * 8 + [20.], .1)["status"], "pass")

    def test_invalid_samples(self):
        for values in ([0.] * 9, [float("nan")] * 9, [1.] * 3):
            with self.assertRaises(ValueError):
                gate.assess(values, values, .1)

    def test_output_mismatch_is_not_a_speedup(self):
        with patch.object(gate, "run_once", side_effect=[(.1, "correct"), (.01, "wrong")]):
            with self.assertRaisesRegex(ValueError, "output mismatch"):
                gate.measure_case("old", "new", Path("test.py"), 9, 1, 60, .1)

    def test_measurement_balances_first_and_second_process_positions(self):
        invocation = 0

        def position_sensitive_run(executable, source, timeout):
            nonlocal invocation
            position = invocation % 2
            invocation += 1
            elapsed = {
                ("old", 0): 1.0, ("old", 1): 2.0,
                ("new", 0): .8, ("new", 1): 2.0,
            }[(executable, position)]
            return elapsed, "same output"

        with patch.object(gate, "run_once", side_effect=position_sensitive_run):
            result = gate.measure_case("old", "new", Path("test.py"), 9, 1, 60, .1)
        self.assertEqual(result["status"], "pass")
        self.assertAlmostEqual(result["ratio"], (0.8 ** .5), places=12)
        self.assertEqual(len(result["order_balanced_seconds"]["baseline_first"]), 9)

    def test_runtime_hash_includes_dll(self):
        with tempfile.TemporaryDirectory() as directory:
            exe = Path(directory) / "xlang3.exe"
            dll = Path(directory) / "xlang3_runtime.dll"
            exe.write_bytes(b"launcher")
            dll.write_bytes(b"old runtime")
            before = gate.runtime_identity(exe)
            dll.write_bytes(b"new runtime")
            self.assertNotEqual(before, gate.runtime_identity(exe))

    def test_missing_baseline_exits_invalid_and_writes_report(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "report.json"
            self.assertEqual(gate.main(["--baseline", str(Path(directory) / "missing"),
                                        "--output", str(output)]), 2)
            self.assertIn('"status": "invalid"', output.read_text())


if __name__ == "__main__":
    unittest.main()
