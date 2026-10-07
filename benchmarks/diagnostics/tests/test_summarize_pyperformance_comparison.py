"""Ensure full-run reports never turn failed worker values into speed scores."""
import contextlib
import csv
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


source = Path(__file__).resolve().parents[1] / 'summarize_pyperformance_comparison.py'
spec = importlib.util.spec_from_file_location('full_summary', source)
summary = importlib.util.module_from_spec(spec)
spec.loader.exec_module(summary)


class FullSummaryTests(unittest.TestCase):
    def test_chart_covers_ratios_outside_original_axis(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'chart.svg'
            summary.chart_svg(path, [{'subtest': 'slow', 'speedup': 0.0085},
                                     {'subtest': 'gc', 'speedup': 3.124}],
                              completed=2, failed=95, geomean=0.163)
            chart = path.read_text(encoding='utf-8')
            self.assertIn('>0.005×</text>', chart)
            self.assertIn('>5×</text>', chart)
            self.assertIn('>3.12×</text>', chart)
            self.assertNotIn('width="1.0" height="14"', chart)
            self.assertIn('class="baseline"', chart)

    def prepare(self, root):
        data = root / 'data'
        data.mkdir()
        names = [f'case_{index:02}' for index in range(97)]
        for runtime, duration in [('xlang', 2.0), ('cpython', 1.0)]:
            (data / f'{runtime}.json').write_text(json.dumps({'benchmarks': [
                {'metadata': {'name': name}, 'runs': [{'values': [duration]}]}
                for name in names]}), encoding='utf-8')
            (data / f'{runtime}.log').write_text('\n'.join(
                f'[{index + 1}/97] {name}...' for index, name in enumerate(names)),
                encoding='utf-8')
        with (data / 'canonical.csv').open('w', newline='', encoding='utf-8') as stream:
            writer = csv.DictWriter(stream, fieldnames=[
                'benchmark', 'CPython 3.14 status', 'CPython subtests',
                'XLang3 subtests', 'CPython failure detail'])
            writer.writeheader()
            writer.writerows({'benchmark': name, 'CPython 3.14 status': 'completed',
                              'CPython subtests': name + '=1 s',
                              'XLang3 subtests': name + '=2 s',
                              'CPython failure detail': 'obsolete failure'} for name in names)
        return data

    def run_summary(self, data, extra=()):
        arguments = ['summary', '--xlang-json', str(data / 'xlang.json'),
                     '--cpython-json', str(data / 'cpython.json'),
                     '--xlang-log', str(data / 'xlang.log'),
                     '--cpython-log', str(data / 'cpython.log'),
                     '--canonical-status', str(data / 'canonical.csv'),
                     '--output-dir', str(data), '--prefix', 'test-report',
                     '--release-exe-sha256', 'test-exe',
                     '--release-dll-sha256', 'test-dll']
        arguments.extend(extra)
        with patch.object(sys, 'argv', arguments), contextlib.redirect_stdout(io.StringIO()):
            summary.main()

    def read_csv(self, path):
        with path.open(newline='', encoding='utf-8-sig') as stream:
            return list(csv.DictReader(stream))

    def test_failed_partial_values_have_no_speed_scores(self):
        with tempfile.TemporaryDirectory() as directory:
            data = self.prepare(Path(directory))
            with (data / 'xlang.log').open('a', encoding='utf-8') as stream:
                stream.write('\n- case_00 (Benchmark timed out)\n')
            with (data / 'cpython.log').open('a', encoding='utf-8') as stream:
                stream.write('\n- case_01 (Benchmark died)\n')
            self.run_summary(data)
            statuses = {row['benchmark']: row for row in self.read_csv(data / 'test-report-all-97-status.csv')}
            self.assertEqual(len(statuses), 97)
            self.assertEqual(statuses['case_00']['XLang3 subtests'], '')
            self.assertEqual(statuses['case_01']['CPython subtests'], '')
            self.assertEqual(statuses['case_01']['CPython 3.14 status'], 'failed: Benchmark died')
            self.assertEqual(statuses['case_02']['CPython failure detail'], '')
            rows = self.read_csv(data / 'test-report-subtests.csv')
            self.assertNotIn('case_00', {row['benchmark'] for row in rows})
            cp_failed = next(row for row in rows if row['benchmark'] == 'case_01')
            self.assertEqual(cp_failed['CPython / XLang3 speedup'], '')
            chart = (data.parent / 'test-report.svg').read_text(encoding='utf-8')
            self.assertNotIn('case_00', chart)
            self.assertNotIn('case_01', chart)
            report = (data.parent / 'test-report.md').read_text(encoding='utf-8')
            self.assertIn('Of **95** matched subtests', report)
            self.assertIn('**0.50000×**', report)

    def test_all_success_does_not_claim_suite_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            data = self.prepare(Path(directory))
            self.run_summary(data)
            report = (data.parent / 'test-report.md').read_text(encoding='utf-8')
            self.assertIn('Every definition completed.', report)
            self.assertNotIn('exit code 1', report)
            self.assertIn('cpython.log', report)

    def test_fresh_subtests_are_mapped_when_historical_definition_failed(self):
        with tempfile.TemporaryDirectory() as directory:
            data = self.prepare(Path(directory))
            canonical = self.read_csv(data / 'canonical.csv')
            canonical[0]['CPython subtests'] = ''
            canonical[0]['XLang3 subtests'] = ''
            canonical[0]['CPython 3.14 status'] = 'failed: Benchmark died'
            with (data / 'canonical.csv').open('w', newline='', encoding='utf-8') as stream:
                writer = csv.DictWriter(stream, fieldnames=list(canonical[0]))
                writer.writeheader()
                writer.writerows(canonical)
            for runtime, duration in [('xlang', 2.0), ('cpython', 1.0)]:
                path = data / (runtime + '.json')
                payload = json.loads(path.read_text(encoding='utf-8'))
                payload['benchmarks'][0]['metadata']['name'] = 'new_subtest_a'
                payload['benchmarks'].append({'metadata': {'name': 'new_subtest_b'},
                                               'runs': [{'values': [duration]}]})
                path.write_text(json.dumps(payload), encoding='utf-8')
                path = data / (runtime + '.log')
                log = path.read_text(encoding='utf-8')
                log = log.replace('[1/97] case_00...', '[1/97] case_00...\n'
                                  'new_subtest_a: Mean +- std dev: 1 sec +- 0 sec\n'
                                  'new_subtest_b: Mean +- std dev: 1 sec +- 0 sec')
                path.write_text(log, encoding='utf-8')
            self.run_summary(data)
            statuses = self.read_csv(data / 'test-report-all-97-status.csv')
            self.assertEqual(statuses[0]['CPython 3.14 status'], 'completed')
            self.assertIn('new_subtest_b=', statuses[0]['CPython subtests'])
            rows = self.read_csv(data / 'test-report-subtests.csv')
            fresh = [row for row in rows if row['benchmark'] == 'case_00']
            self.assertEqual({row['subtest'] for row in fresh}, {'new_subtest_a', 'new_subtest_b'})
            self.assertTrue(all(row['CPython / XLang3 speedup'] == '0.5' for row in fresh))

    def test_recorded_subtest_without_definition_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            data = self.prepare(Path(directory))
            path = data / 'cpython.json'
            payload = json.loads(path.read_text(encoding='utf-8'))
            payload['benchmarks'].append({'metadata': {'name': 'unattributed'},
                                          'runs': [{'values': [1.0]}]})
            path.write_text(json.dumps(payload), encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'subtests lack benchmark definitions'):
                self.run_summary(data)

    def test_incorrect_workload_retains_timings_but_has_no_speed_score(self):
        with tempfile.TemporaryDirectory() as directory:
            data = self.prepare(Path(directory))
            evidence = data / 'correctness.json'
            evidence.write_text('{"rows": 0}', encoding='utf-8')
            self.run_summary(data, ['--invalid-subtest', 'case_00=rendered no rows',
                                    '--correctness-evidence', str(evidence)])
            rows = self.read_csv(data / 'test-report-subtests.csv')
            invalid = next(row for row in rows if row['subtest'] == 'case_00')
            self.assertEqual(invalid['XLang3 seconds'], '2')
            self.assertEqual(invalid['CPython 3.14 seconds'], '1')
            self.assertEqual(invalid['CPython / XLang3 speedup'], '')
            self.assertEqual(invalid['result'], 'invalid: rendered no rows')
            chart = (data.parent / 'test-report.svg').read_text(encoding='utf-8')
            self.assertNotIn('case_00', chart)
            statuses = self.read_csv(data / 'test-report-all-97-status.csv')
            self.assertEqual(statuses[0]['correctness detail'], 'case_00: rendered no rows')
            report = (data.parent / 'test-report.md').read_text(encoding='utf-8')
            self.assertIn('Of **96** matched subtests', report)
            self.assertIn('correctness.json', report)

    def test_correctness_exclusion_requires_evidence(self):
        with tempfile.TemporaryDirectory() as directory:
            data = self.prepare(Path(directory))
            with self.assertRaisesRegex(ValueError, 'require an evidence file'):
                self.run_summary(data, ['--invalid-subtest', 'case_00=missing rows'])

    def test_incomplete_fresh_cpython_log_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            data = self.prepare(Path(directory))
            (data / 'cpython.log').write_text('[1/97] case_00...\n', encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'CPython log is not a complete'):
                self.run_summary(data)

    def test_missing_fresh_cpython_outcome_is_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            data = self.prepare(Path(directory))
            path = data / 'cpython.json'
            payload = json.loads(path.read_text(encoding='utf-8'))
            payload['benchmarks'].pop()
            path.write_text(json.dumps(payload), encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'CPython benchmark outcomes are missing'):
                self.run_summary(data)


if __name__ == '__main__':
    unittest.main()
