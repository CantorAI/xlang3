"""Seven alternating pairs of the unchanged slot and original SQLGlot probes."""
import argparse
import json
import statistics

from canonical_slot_r4_support_20261008 import (
    CANDIDATE, CONTROL, CPYTHON, ROOT, CheckFailure,
    DiagnosticRun, digest, environment, snapshot,
)

parser = argparse.ArgumentParser()
parser.add_argument('--allowed-controller-pid', type=int)
args = parser.parse_args()
run = DiagnosticRun('canonical-slot-r4-paired-20261008',
                    'Diagnostic including unchanged original SQLGlot body; not official scores',
                    args.allowed_controller_pid)
failure = None
try:
    run.initialize()
    early_output = ROOT / 'doc/performance/data/canonical-slot-r4-early-20261008.json'
    early = json.loads(early_output.read_text(encoding='utf-8'))
    if early['hashes_before'] != snapshot():
        raise CheckFailure('Early correctness hashes must still match this paired trial')
    required = {'cpython3147', 'control', 'candidate', 'cpp'}
    passed = {phase['name'] for phase in early['phases'] if phase['status'] == 'passed'}
    if not required.issubset(passed):
        raise CheckFailure('Fresh early fixture and CPP phases must all pass first')
    if args.allowed_controller_pid is not None and args.allowed_controller_pid not in early['owned_controller_pids']:
        raise CheckFailure('Allowed parent PID must be the recorded early controller')
    run.record['early_correctness'] = {'path': early_output.name,
                                      'verified_phase_names': sorted(required)}
    run.record['probes'] = []
    run.idle_guard('start')
    executables = {'control': CONTROL, 'candidate': CANDIDATE}
    identity = lambda row: {key: value for key, value in row.items() if key != 'samples_seconds'}
    for label, filename, count in (('slot', 'canonical-slot-probe-20261008.py', 3),
                                   ('sqlglot_parse', 'canonical-slot-sqlglot-probe-20261008.py', 1)):
        probe = ROOT / 'scratch/performance' / filename
        observed = {'name': label, 'filename': filename, 'probe_sha256': digest(probe), 'pairs': []}
        run.record['probes'].append(observed)
        run.save()

        def decode_rows(stdout):
            value = json.loads(stdout)
            if len(value['rows']) != count:
                raise CheckFailure('Unexpected number of workload rows')
            for row in value['rows']:
                if len(row['samples_seconds']) != 5:
                    raise CheckFailure('Expected five unchanged samples per row')
                if label == 'slot':
                    if row['operations'] != 16384 or row['checksum'] != 16384 * 7:
                        raise CheckFailure('Slot workload/checksum changed')
                elif row['parses_per_sample'] != 10:
                    raise CheckFailure('Original SQLGlot workload changed')
            return value

        run.idle_guard(label + '-before-reference')
        raw = run.phase(label + '-cpython3147', [str(CPYTHON), str(probe)], environment(True), 120,
                        lambda stdout: decode_rows(stdout))
        observed['cpython3147'] = decode_rows(raw)
        run.save()
        for number in range(7):
            run.idle_guard(label + '-before-pair-' + str(number + 1))
            order = ('control', 'candidate') if number % 2 == 0 else ('candidate', 'control')
            pair = {'order': order, 'runs': {}}
            observed['pairs'].append(pair)
            run.save()
            for name in order:
                def validate_run(stdout):
                    decoded = decode_rows(stdout)
                    if [identity(row) for row in decoded['rows']] != [
                            identity(row) for row in observed['cpython3147']['rows']]:
                        raise CheckFailure('Workload identity must match CPython 3.14.7')
                raw = run.phase(label + '-pair-' + str(number + 1) + '-' + name,
                                [str(executables[name]), str(probe)], environment(True), 120, validate_run)
                pair['runs'][name] = decode_rows(raw)
                run.save()
            run.idle_guard(label + '-after-pair-' + str(number + 1))
            print('Finished', label, 'pair', number + 1, flush=True)
        observed['summary'] = []
        for index, row in enumerate(observed['pairs'][0]['runs']['control']['rows']):
            ratios = [statistics.median(pair['runs']['control']['rows'][index]['samples_seconds']) /
                      statistics.median(pair['runs']['candidate']['rows'][index]['samples_seconds'])
                      for pair in observed['pairs']]
            observed['summary'].append({'identity': identity(row),
                'control_time_over_candidate_time': ratios,
                'median_speedup': statistics.median(ratios),
                'pairs_favoring_candidate': sum(value > 1 for value in ratios)})
        run.save()
        print(label, 'summary:', json.dumps(observed['summary']), flush=True)
    run.idle_guard('terminal')
except Exception as exc:
    failure = exc
finally:
    run.finish(failure)
