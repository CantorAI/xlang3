"""Require fresh CPython/control/candidate/CPP correctness before Inherited-slot proof timings."""
import json
import os
import sys

from inherited_slot_proof_support_20261008 import (
    CANDIDATE, CONTROL, CPYTHON, CTEST, ROOT, CheckFailure,
    DiagnosticRun, digest, environment,
)

run = DiagnosticRun('inherited-slot-proof-early-20261008',
                    'Fresh focused correctness followed by unchanged paired diagnostics')
failure = None
try:
    run.initialize()
    run.idle_guard('start')
    fixture = ROOT / 'tests/fixtures/core/canonical_slot_reads.py'
    expected = (ROOT / 'tests/fixtures/expected/canonical_slot_reads.out').read_text(
        encoding='utf-8').replace('\r\n', '\n').strip()

    def validate_fixture(stdout):
        if stdout.decode('utf-8').replace('\r\n', '\n').strip() != expected:
            raise CheckFailure('Fixture output differs from checked-in expected output')

    for label, executable in (('cpython3147', CPYTHON), ('control', CONTROL), ('candidate', CANDIDATE)):
        run.phase(label, [str(executable), str(fixture)], environment(), 90, validate_fixture)
        print('Passed', label, flush=True)

    def validate_cpp(stdout):
        if '100% tests passed, 0 tests failed out of 1' not in stdout.decode('utf-8', errors='replace'):
            raise CheckFailure('ctest must execute and pass the interpreter target')

    run.phase('cpp', [CTEST, '--test-dir', 'build-repro/main-verify-20261006',
                     '-C', 'Release', '-R', 'xlang3_interpreter_tests', '--output-on-failure'],
              environment(), 120, validate_cpp)
    print('Passed cpp', flush=True)
    run.idle_guard('before-paired')
    paired = ROOT / 'scratch/performance/compare-inherited-slot-proof-20261008.py'
    run.phase('paired', [str(CPYTHON), str(paired), '--allowed-controller-pid', str(os.getpid())],
              environment(), 300)
    paired_output = ROOT / 'doc/performance/data/inherited-slot-proof-paired-20261008.json'
    paired_record = json.loads(paired_output.read_text(encoding='utf-8'))
    if paired_record['status'] != 'terminal' or not paired_record['hashes_unchanged']:
        raise CheckFailure('Paired child must be terminal with unchanged hashes')
    run.record['paired_result'] = {'path': paired_output.name, 'sha256': digest(paired_output)}
except Exception as exc:
    failure = exc
finally:
    run.finish(failure)
print('Inherited-slot proof early correctness and paired diagnostics terminal', flush=True)
