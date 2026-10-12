"""Root-only file export to a new scratch preview. No tests, benchmarks or Git writes."""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import statistics
import subprocess
import sys

ROOT = Path('D:/CantorAI/xlang3').resolve()
SCRATCH = ROOT / 'scratch/performance'
DATA = ROOT / 'doc/performance/data'
HEAD = '87f5293782d21592df3bcc4ea924e659c906b6da'
PLAN = SCRATCH / 'triple-string-closing-comment-checkpoint-export-inputs-proposed-20261008.json'
PROOF = SCRATCH / 'triple-string-closing-comment-checkpoint-export-provenance-proposed-20261008.json'
MANIFEST = 'doc/performance/data/triple-string-closing-comment-checkpoint-publication-20261008.json'
sha = lambda raw: hashlib.sha256(raw).hexdigest()

def load(path):
    return json.loads(path.read_bytes())

def pin(path, expected):
    raw = path.read_bytes()
    assert sha(raw) == expected, str(path)
    return raw

def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)

def map_check(mapping, base):
    for path, expected in mapping.items():
        pin(base / path, expected)

def tree(root):
    assert not root.is_symlink()
    result = {}
    for path in root.rglob('*'):
        assert not path.is_symlink(), str(path)
        if path.is_file():
            result[path.relative_to(root).as_posix()] = sha(path.read_bytes())
    return result

def csv_bytes(fields, rows):
    text = io.StringIO(newline='')
    writer = csv.DictWriter(text, fieldnames=fields, lineterminator='\n')
    writer.writeheader()
    writer.writerows(rows)
    return text.getvalue().encode()

def dirty():
    return {p: sha((ROOT / p).read_bytes()) for p in
            git('diff', 'HEAD', '--name-only', '-z').decode().split('\0') if p}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan-sha256', required=True)
    parser.add_argument('--proof-sha256', required=True)
    args = parser.parse_args()
    assert sys.version_info[:3] == (3, 14, 7) and sys.flags.optimize == 0
    assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
    assert Path.cwd().resolve() == ROOT
    plan = json.loads(pin(PLAN, args.plan_sha256))
    proof = json.loads(pin(PROOF, args.proof_sha256))
    assert proof['plan_sha256'] == args.plan_sha256
    assert proof['controller_sha256'] == sha(Path(__file__).read_bytes())
    assert git('rev-parse', 'HEAD').decode().strip() == HEAD
    index_before = sha(git('ls-files', '--stage', '-z'))
    assert not git('diff', '--cached', '--name-only', '-z')
    dirty_before = dirty()
    attrs_before = (ROOT / '.gitattributes').read_bytes()
    validation = json.loads(pin(ROOT / plan['validation'], plan['validation_sha256']))
    assert validation['status'] == 'trial_validation_failed'
    assert validation['terminal'] and validation['hashes_unchanged']
    assert validation['correctness_passed'] and not validation['full_validated']
    assert not validation['correctness_reused_same_candidate']
    assert validation['source_count'] == 124 and len(validation['source_sha256']) == 124
    assert validation['fixture_counts'] == {'core': 399, 'compatibility_sections': 11, 'expected_failure_cases': 3}
    assert validation['ctest_count'] == 9 and validation['native_api_checks'] == 2
    assert validation['controller_sha256'] == plan['validation_controller_sha256']
    map_check(validation['source_sha256'], ROOT)
    map_check(validation['binaries_sha256'], ROOT)
    baseline = ROOT / plan['fixed_baseline_root']
    assert len(validation['binaries_sha256']) == 178 and len(validation['baseline_sha256']) == 177
    assert tree(baseline) == validation['baseline_sha256']
    release = ROOT / plan['release_root']
    assert tree(release) == {Path(p).relative_to(Path(plan['release_root'])).as_posix(): h
                             for p, h in validation['binaries_sha256'].items()}
    map_check(validation['hashes_before'], ROOT)
    assert validation['hashes_before'] == validation['hashes_after']
    for row in validation['phases']:
        for stream in ('stdout', 'stderr'):
            pin(DATA / row[stream + '_log'], row[stream + '_sha256'])
        watch = row['external_process_watch']
        pin(DATA / watch['log'], watch['sha256'])
        assert row['owned_child_cleanup_completed']
        assert row['post_idle_guard_passed'] and row['post_hashes_stable']
        if row['timed']:
            assert watch['measurement_valid'] and row['measurement_valid']
            assert not watch['overlaps'] and not watch['scanner_errors']
        else:
            assert row['semantic_passed'] and row['passed'] and row['exit_code'] == 0
            assert not row['timing_accepted']
    ctest = next(row for row in validation['phases'] if row['name'] == 'ctest')
    assert not ctest['measurement_valid'] and ctest['semantic_passed']
    assert all(item['Name'].lower() == 'ctest.exe' and item['ProcessId'] == ctest['pid']
               for observed in ctest['external_process_watch']['overlaps'] for item in observed['busy'])
    gate = json.loads(pin(DATA / validation['fixed_gate']['output'], validation['fixed_gate']['sha256']))
    assert validation['fixed_gate']['exit_code'] == 0 and gate['status'] == 'pass'
    assert (gate['repeats'], gate['warmup'], gate['threshold']) == (21, 5, .10)
    assert len(gate['cases']) == 11 and all(c['status'] == 'pass' for c in gate['cases'].values())
    base_archive = ROOT / plan['published_source119_root']
    parent = json.loads(pin(ROOT / plan['parent_manifest'], plan['parent_manifest_sha256']))
    for path, expected in parent['source_snapshot_sha256'].items():
        if path not in plan['additional_parser_paths']:
            assert validation['source_sha256'][path] == expected
            pin(base_archive / path, expected)
    assert len(parent['source_snapshot_sha256']) == 124 and len(plan['additional_parser_paths']) == 5
    for row in plan['files']:
        assert row['path'].startswith('doc/performance/')
        raw = pin(ROOT / row['source'], row['sha256'])
        assert len(raw) == row['bytes']
        live = ROOT / row['path']
        if live.exists():
            assert live.read_bytes() == raw, str(live)
    names = [row['path'] for row in plan['files']]
    assert len(names) == len(set(names))
    for path, expected in plan['owned_source_sha256'].items():
        pin(ROOT / path, expected)
        original = (ROOT / plan['owned_input_paths'][path]).read_bytes()
        assert original.replace(b'\r\n', b'\n') == git('show', 'HEAD:' + path).replace(b'\r\n', b'\n')

    official_rows = []
    failure_rows = []
    for benchmark in ('sqlalchemy_declarative', 'sqlalchemy_imperative'):
        result = validation['official_results'][benchmark]['cpython3147']
        raw = json.loads(pin(DATA / result['output'], result['sha256']))
        assert len(raw['benchmarks']) == 1
        values = []
        for run_index, run in enumerate(raw['benchmarks'][0]['runs']):
            for value_index, value in enumerate(run.get('values', [])):
                values.append(value)
                official_rows.append({'benchmark': benchmark, 'runtime': 'cpython3147',
                                      'run_index': run_index, 'value_index': value_index, 'seconds': value})
        assert len(values) == 20 and statistics.mean(values) == result['mean_seconds']
        row = next(r for r in validation['phases'] if r['name'] == 'official-xlang3-' + benchmark)
        assert row['exit_code'] == 1 and not row['passed'] and not row['timing_accepted']
        assert 'property getter is not callable' in (DATA / row['stderr_log']).read_text()
        attempt = validation['official_attempts'][row['name']]
        assert not attempt['complete'] and attempt['sha256'] is None
        assert not (DATA / attempt['output']).exists()
        failure_rows.append({'benchmark': benchmark, 'runtime': 'xlang3', 'exit_code': row['exit_code'],
                             'complete_score': False, 'error': 'RuntimeError: property getter is not callable',
                             'stdout_log': row['stdout_log'], 'stderr_log': row['stderr_log']})
    assert len(official_rows) == 40
    summaries, arrays = [], []
    for name, case in gate['cases'].items():
        last = case['attempts'][-1]
        summaries.append({'case': name, 'status': case['status'], 'attempt_count': len(case['attempts']),
                          'ratio_candidate_over_baseline': last['ratio'],
                          'ci95_lower': last['ratio_interval_95'][0], 'ci95_upper': last['ratio_interval_95'][1]})
        for attempt_index, attempt in enumerate(case['attempts']):
            sources = [('baseline_seconds', attempt['baseline_seconds']), ('candidate_seconds', attempt['candidate_seconds'])]
            sources += list(attempt['order_balanced_seconds'].items())
            for series, values in sources:
                assert len(values) == 21
                arrays.extend({'case': name, 'attempt_index': attempt_index, 'series': series,
                               'value_index': i, 'seconds': value} for i, value in enumerate(values))

    preview = ROOT / plan['preview_root']
    assert preview.is_relative_to(SCRATCH) and not preview.exists()
    preview.mkdir()
    publication = preview / 'publication'
    generated = []
    def put(path, raw, kind):
        target = publication / path
        assert target.resolve().is_relative_to(publication.resolve())
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(raw)
        generated.append({'path': path, 'sha256': sha(raw), 'bytes': len(raw), 'kind': kind})
    for row in plan['files']:
        put(row['path'], (ROOT / row['source']).read_bytes(), row['kind'])
    archive = plan['archive_root']
    for source, raw in ((Path(__file__), Path(__file__).read_bytes()),
                        (PLAN, pin(PLAN, args.plan_sha256)), (PROOF, pin(PROOF, args.proof_sha256))):
        put(archive + '/controllers/' + source.name, raw, 'frozen_export_metadata')
    put('doc/performance/triple-string-closing-comment-checkpoint-20261008.md',
        pin(ROOT / plan['report_template'], plan['report_template_sha256']), 'report')
    put('doc/performance/data/triple-string-closing-comment-official-cpython-values-20261008.csv',
        csv_bytes(['benchmark', 'runtime', 'run_index', 'value_index', 'seconds'], official_rows), 'all_40_official_values')
    put('doc/performance/data/triple-string-closing-comment-official-xlang3-failures-20261008.csv',
        csv_bytes(['benchmark', 'runtime', 'exit_code', 'complete_score', 'error', 'stdout_log', 'stderr_log'], failure_rows), 'two_failed_attempts')
    put('doc/performance/data/triple-string-closing-comment-fixed-gate-summary-20261008.csv',
        csv_bytes(list(summaries[0]), summaries), 'fixed_gate_summary')
    put('doc/performance/data/triple-string-closing-comment-fixed-gate-values-20261008.csv',
        csv_bytes(['case', 'attempt_index', 'series', 'value_index', 'seconds'], arrays), 'all_fixed_gate_arrays')
    attrs_head = git('show', 'HEAD:.gitattributes')
    attrs_candidate = attrs_head + (b'' if attrs_head.endswith(b'\n') else b'\n') + b'\n' + ('\n'.join(plan['owned_attribute_additions']) + '\n').encode()
    stage = preview / 'staging'
    stage.mkdir()
    (stage / 'gitattributes-head-plus-owned').write_bytes(attrs_candidate)
    staging = {'scope': 'Scratch-only blobs; root stages HEAD plus owned additions independently.',
               'owned_source_sha256': plan['owned_source_sha256'],
               'attributes_head_sha256': sha(attrs_head), 'attributes_candidate_sha256': sha(attrs_candidate),
               'attributes_candidate': 'staging/gitattributes-head-plus-owned',
               'working_attributes_sha256': sha(attrs_before), 'owned_attribute_additions': plan['owned_attribute_additions']}
    (preview / 'staging-plan.json').write_text(json.dumps(staging, indent=2) + '\n', encoding='utf-8')
    assert len(generated) == len({r['path'] for r in generated})
    manifest = {'status': 'parser_correctness_and_default_gate_passed_official_x_failures_retained',
                'terminal': True, 'full_validated': False, 'whole97_rerun': False,
                'files': generated, 'owned_source_sha256': plan['owned_source_sha256'],
                'validation_sha256': plan['validation_sha256'], 'source_count': 124,
                'binary_count': 178, 'fixed_baseline_count': 177, 'correctness_passed': True,
                'fixed_gate_passed': True, 'official_cp_values': 40, 'official_x_completed_scores': 0,
                'plan_sha256': args.plan_sha256, 'controller_sha256': sha(Path(__file__).read_bytes()),
                'proof_sha256': args.proof_sha256, 'staging_metadata_scope': 'Scratch preview only; not a published relative link.',
                'published_source119_reference': plan['published_source119_root'],
                'historical_native_diagnostic_scope': plan['historical_native_diagnostic_scope']}
    manifest_path = publication / MANIFEST
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    assert attrs_before == (ROOT / '.gitattributes').read_bytes()
    assert dirty_before == dirty() and index_before == sha(git('ls-files', '--stage', '-z'))
    map_check(validation['source_sha256'], ROOT)
    map_check(validation['binaries_sha256'], ROOT)
    assert tree(baseline) == validation['baseline_sha256']
    for row in plan['files']:
        pin(ROOT / row['source'], row['sha256'])
    print(json.dumps({'status': 'scratch_preview_exported', 'preview': str(preview),
                      'manifest': str(manifest_path), 'manifest_sha256': sha(manifest_path.read_bytes()),
                      'file_count_without_manifest': len(generated), 'live_files_unchanged': True}))

if __name__ == '__main__':
    main()
