"""Run every golden fixture; compare failures with a fixed accepted runtime.

Existing baseline failures remain failures in the report. This checks that the
candidate introduces none; it never claims a clean suite when baseline defects
or platform-specific expectations remain. New cases must pass their golden.
Run with XLang3. Performance is a separate, unchanged release gate.
"""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import sys

assert sys.implementation.name == 'xlang3'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', required=True)
    parser.add_argument('--candidate', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--new-case', action='append', default=[])
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    spec = importlib.util.spec_from_file_location('release_fixture_runner', root / 'run_fixtures.py')
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)
    results = []

    def trial(executable, operation):
        # Each child must load its own .so. Inheriting the native comparison
        # driver's candidate LD_LIBRARY_PATH would falsely test that runtime
        # twice even though the executable paths differ.
        previous = os.environ.get('LD_LIBRARY_PATH')
        os.environ['LD_LIBRARY_PATH'] = str(Path(executable).resolve().parent)
        try:
            operation(executable)
        finally:
            if previous is None:
                os.environ.pop('LD_LIBRARY_PATH', None)
            else:
                os.environ['LD_LIBRARY_PATH'] = previous

    def check(name, operation):
        try:
            trial(args.candidate, operation)
        except Exception as candidate_error:
            candidate_error = str(candidate_error)
            if name in args.new_case:
                results.append({'case': name, 'status': 'regression', 'candidateError': candidate_error})
                return
            try:
                trial(args.baseline, operation)
            except Exception as baseline_error:
                baseline_error = str(baseline_error)
                results.append({'case': name,
                    'status': 'baseline-failure' if candidate_error == baseline_error else 'regression',
                    'candidateError': candidate_error, 'baselineError': baseline_error})
            else:
                results.append({'case': name, 'status': 'regression', 'candidateError': candidate_error})
        else:
            results.append({'case': name, 'status': 'passed'})
        if len(results) % 25 == 0:
            print('Fixture progress:', len(results), flush=True)

    for folder, cases in [('core', runner.CORE_CASES), ('compat_sections', runner.SECTION_CASES)]:
        for name in cases:
            fixture = root / 'fixtures' / folder / (name + '.py')
            expected = root / 'fixtures/expected' / ('' if folder == 'core' else folder) / (name + '.out')
            check(name, lambda executable, fixture=fixture, expected=expected:
                runner.run_case(executable, fixture, expected, root))
    for name, fragments in [
        ('uncaught_exception', ('Traceback (most recent call last):', 'RuntimeError: top')),
        ('uncaught_runtime_error', ('Traceback (most recent call last):', 'ZeroDivisionError: division by zero')),
        ('unset_instance_attr', ("Traceback (most recent call last):", "AttributeError: 'A' object has no attribute 'x'")),
    ]:
        fixture = root / 'fixtures/core' / (name + '.py')
        check(name, lambda executable, fixture=fixture, fragments=fragments:
            runner.assert_failure(executable, fixture, fragments))
    regressions = [item for item in results if item['status'] == 'regression']
    existing = [item for item in results if item['status'] == 'baseline-failure']
    report = {'runtime': sys.implementation.name, 'cases': len(results),
        'candidateGoldenPasses': sum(item['status'] == 'passed' for item in results),
        'existingBaselineFailures': existing, 'newRegressions': regressions,
        'completeGoldenSuitePassed': not regressions and not existing,
        'relativeCorrectnessPassed': not regressions,
        'results': results}
    args.output.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k not in ['results', 'existingBaselineFailures', 'newRegressions']}, indent=2))
    return 1 if regressions else 0


if __name__ == '__main__':
    raise SystemExit(main())
