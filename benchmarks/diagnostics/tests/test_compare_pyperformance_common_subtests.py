"""Protect completed-run provenance checks used by performance reports."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from compare_pyperformance_common_subtests import validate_completed_provenance


class ProvenanceTests(unittest.TestCase):
    def setUp(self):
        identity = {'exe': 'a' * 64, 'dll': 'b' * 64, 'hashlib': 'c' * 64}
        self.record = {
            'status': 'finished_with_benchmark_failures',
            'sha256_start': identity.copy(), 'sha256_end': identity.copy(),
            'manager_version': '3.14.7', 'cpython_reference': 'reference.json',
        }

    def validate(self, record):
        validate_completed_provenance(record, 'reference.json')

    def test_required_version_encodings(self):
        for version in ('3.14.7', 'Python 3.14.7',
                        '3.14.7 (tags/v3.14.7:823f032, Aug 5 2026) [MSC v.1944]'):
            with self.subTest(version=version):
                self.validate(dict(self.record, manager_version=version))

    def test_wrong_or_malformed_versions(self):
        for version in ('3.13.7', '3.14.6', '3.14.70', '3.14.7rc1',
                        'noise 3.14.7', '3.14.7 invalid', ''):
            with self.subTest(version=version), self.assertRaises(ValueError):
                self.validate(dict(self.record, manager_version=version))

    def test_running_record_rejected(self):
        with self.assertRaises(ValueError):
            self.validate(dict(self.record, status='running'))

    def test_native_package_change_rejected(self):
        record = copy.deepcopy(self.record)
        record['sha256_end']['hashlib'] = 'd' * 64
        with self.assertRaises(ValueError):
            self.validate(record)

    def test_different_reference_rejected(self):
        with self.assertRaises(ValueError):
            self.validate(dict(self.record, cpython_reference='other.json'))


if __name__ == '__main__':
    unittest.main()
