"""Verify saved evidence; pending official output is explicitly incomplete."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import re

parser = argparse.ArgumentParser()
parser.add_argument('--require-terminal', action='store_true')
args = parser.parse_args()
root = Path.cwd()
data = root / 'doc/performance/data'
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
trial = json.loads((data / 'dict-native-index-final-validation-20261007.json').read_text(encoding='utf-8'))
assert [phase['name'] for phase in trial['phases'][:4]] == ['fixtures', 'cpp', 'scaling', 'gate']
assert all(phase['exit_code'] == 0 for phase in trial['phases'][:4])
for phase in trial['phases']:
    assert digest(data / phase['log']) == phase['sha256'], phase['log']
for name, expected in trial['source_sha256'].items():
    assert digest(root / name) == expected, name
candidate = root / 'build-repro/main-verify-20261006/Release/xlang3.exe'
assert digest(candidate) == trial['candidate_binary_sha256']['exe']
assert digest(candidate.with_name('xlang3_runtime.dll')) == trial['candidate_binary_sha256']['dll']
gate_phase = trial['phases'][3]
gate = Path(gate_phase['command'][gate_phase['command'].index('--output') + 1])
if 'fixed_gate' in trial:
    assert gate.name == trial['fixed_gate']['output']
    assert digest(gate) == trial['fixed_gate']['sha256']
gate_record = json.loads(gate.read_text(encoding='utf-8'))
assert gate_record['status'] == 'pass' and len(gate_record['cases']) == 11
assert all(case['status'] == 'pass' for case in gate_record['cases'].values())
assert (gate_record['repeats'], gate_record['warmup'], gate_record['threshold']) == (21, 5, .1)
archive = data / 'dict-intrinsic-index-20261007-sources'
manifest = json.loads((archive / 'manifest.json').read_text(encoding='utf-8'))
for name, expected in manifest['files_sha256'].items():
    assert digest(archive / name) == expected, name
assert digest(data / 'dict-composite-scaling-baseline-20261007.json') == manifest['baseline_sha256']
export = json.loads((data / 'dict-intrinsic-index-20261007-diagnostic-export.json').read_text(encoding='utf-8'))
for item in export['outputs']:
    path = data / item['path']
    assert digest(path) == item['sha256']
    with path.open(encoding='utf-8', newline='') as stream:
        rows = list(csv.DictReader(stream))
    assert len(rows) == item['rows']
    assert all(row['diagnostic_only'] == 'True' for row in rows)
report = root / 'doc/performance/dict-intrinsic-index-checkpoint-20261007.md'
for link in re.findall(r'\]\(([^)]+)\)', report.read_text(encoding='utf-8')):
    assert (report.parent / link).exists(), link
body = json.loads((data / 'bpe-official-body-dict-index-observations-20261007.json').read_text(encoding='utf-8'))
assert body['status'] == 'terminal'
assert [row['runtime'] for row in body['observations']] == ['cpython3147', 'candidate', 'accepted_control']
for row in body['observations']:
    assert digest(data / row['log']) == row['log_sha256']
phases = json.loads((data / 'bpe-dict-index-phase-observations-20261007.json').read_text(encoding='utf-8'))
assert phases['status'] == 'terminal_matching_complete_outputs'
left, right = [row['result'] for row in phases['observations']]
for field in ('vocab_size', 'ranks_sha256', 'encoded_token_count', 'encoded_tokens_sha256'):
    assert left[field] == right[field]
assert left['vocab_size'] == 1024 and left['encoded_token_count'] == 10335
for row in phases['observations']:
    assert digest(data / row['log']) == row['log_sha256']
if args.require_terminal:
    assert trial['status'] != 'running' and len(trial['phases']) == 5
    official = trial['official_bpe']
    if official['sha256'] is not None:
        assert digest(data / official['output']) == official['sha256']
print('Evidence verified: sources/binaries, full correctness, fixed gate, raw logs, 60 medians/180 samples, matching complete BPE output')
print('Official BPE status:', trial['status'], '; pending is not a validated official result')
