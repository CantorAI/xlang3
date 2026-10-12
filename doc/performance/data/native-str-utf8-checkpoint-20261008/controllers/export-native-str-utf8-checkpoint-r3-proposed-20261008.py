"""Root-only, file-only preview export. No benchmarks, live writes or Git mutation."""
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
ARCHIVE = 'doc/performance/data/native-str-utf8-checkpoint-20261008'
MANIFEST = 'doc/performance/data/native-str-utf8-checkpoint-publication-20261008.json'
HEAD = '4d5b87b5b0cb339fa848e63fc896a54eeb39e60c'
sha = lambda raw: hashlib.sha256(raw).hexdigest()

def load(path):
    return json.loads(path.read_bytes())

def pinned(path, expected):
    raw = path.read_bytes()
    assert sha(raw) == expected, str(path)
    return raw

def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)

def verify_map(mapping, base=ROOT):
    for path, expected in mapping.items():
        pinned(base / path, expected)

def dirty():
    return {p: sha((ROOT / p).read_bytes()) for p in
            git('diff', 'HEAD', '--name-only', '-z').decode().split('\0') if p}

def csv_bytes(fields, rows):
    output = io.StringIO(newline='')
    writer = csv.DictWriter(output, fieldnames=fields, lineterminator='\n')
    writer.writeheader()
    writer.writerows(rows)
    return output.getvalue().encode('utf-8')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--plan-sha256', required=True)
    parser.add_argument('--proof-sha256', required=True)
    args = parser.parse_args()
    assert sys.version_info[:3] == (3, 14, 7) and sys.flags.optimize == 0
    assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
    assert Path.cwd().resolve() == ROOT
    plan_path = SCRATCH / 'native-str-utf8-checkpoint-export-r3-inputs-proposed-20261008.json'
    proof_path = SCRATCH / 'native-str-utf8-checkpoint-export-r3-provenance-proposed-20261008.json'
    plan = json.loads(pinned(plan_path, args.plan_sha256))
    proof = json.loads(pinned(proof_path, args.proof_sha256))
    assert proof['plan_sha256'] == args.plan_sha256
    assert proof['controller_sha256'] == sha(Path(__file__).read_bytes())
    assert git('rev-parse', 'HEAD').decode().strip() == HEAD
    assert not git('diff', '--cached', '--name-only')
    assert dirty() == plan['tracked_dirty_sha256']
    verify_map(plan['cpython_binary_sha256'], Path('/'))
    for row in plan['copies']:
        source = ROOT / row['source']
        assert source.resolve().is_relative_to(ROOT)
        assert source.suffix.lower() not in ('.exe', '.dll', '.obj', '.lib', '.pdb')
        assert len(pinned(source, row['sha256'])) == row['bytes']
    validation = load(DATA / 'native-str-utf8-validation-resume-r2-20261008.json')
    assert validation['status'] == 'trial_validated' and validation['terminal']
    assert validation['full_validated'] and validation['correctness_passed']
    assert validation['hashes_unchanged'] and not validation['whole_goal_complete']
    assert validation['fixture_counts'] == dict(core=399, compatibility_sections=11, expected_failure_cases=3)
    assert validation['ctest_count'] == 9 and validation['native_api_checks'] == 2
    assert len(validation['source_sha256']) == 119 and len(validation['binaries_sha256']) == 178
    assert len(validation['baseline_sha256']) == 177
    assert validation['hashes_before'] == validation['hashes_after']
    for row in validation['phases']:
        assert row['exit_code'] == 0 and not row['timeout'] and row['owned_child_cleanup_completed']
        if row.get('execution') == 'historical_same_candidate_untimed_correctness':
            assert row['semantic_passed'] and not row['executed_again'] and not row['timing_accepted']
        else:
            assert row['passed'] and row['measurement_valid']
    old = load(DATA / 'native-str-utf8-validation-20261008.json')
    assert old['status'] == 'trial_validation_failed' and old['hashes_unchanged']
    old_ctest = next(row for row in old['phases'] if row['name'] == 'ctest')
    reused_ctest = next(row for row in validation['phases'] if row['name'] == 'ctest')
    assert not old_ctest['passed'] and not old_ctest['measurement_valid']
    assert not reused_ctest['passed'] and not reused_ctest['measurement_valid']
    assert reused_ctest['semantic_passed'] and reused_ctest['owner_observer_false_positive']
    parent_root = ROOT / plan['parent_root']
    parent = load(parent_root / 'preserved-release-provenance.json')
    assert len(parent['source_snapshot_sha256']) == 117 and len(parent['files_sha256']) == 178
    verify_map(parent['source_snapshot_sha256'], parent_root / 'source-snapshot')
    verify_map(parent['files_sha256'], parent_root / 'Release')
    assert parent['fixed_baseline_sha256'] == validation['baseline_sha256']
    for key, expected in parent['object_snapshot_sha256'].items():
        pinned(parent_root / 'native-objects' / key, expected)
    for mapping in (validation['source_sha256'], validation['binaries_sha256']):
        verify_map(mapping)
    verify_map(validation['baseline_sha256'], ROOT / plan['fixed_baseline_root'])
    for prefix, mapping in ((plan['candidate_release_root'], validation['binaries_sha256']),
                            (plan['fixed_baseline_root'], validation['baseline_sha256'])):
        base = ROOT if prefix == plan['candidate_release_root'] else ROOT / prefix
        actual = {p.relative_to(base).as_posix(): sha(p.read_bytes())
                  for p in (ROOT / prefix).rglob('*') if p.is_file()}
        assert actual == mapping, prefix
    preview = SCRATCH / 'native-str-utf8-checkpoint-export-preview-20261008'
    assert not preview.exists(), 'Immutable preview already exists; no retry or overwrite'
    publication = preview / 'publication'
    publication.mkdir(parents=True)
    rows = []
    def emit(path, raw, kind, source=None):
        relative = Path(path)
        assert relative.as_posix().startswith('doc/performance/') and '..' not in relative.parts
        assert path not in {r['path'] for r in rows}, path
        target = publication / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open('xb') as stream:
            stream.write(raw)
        rows.append(dict(path=path, sha256=sha(raw), bytes=len(raw), kind=kind,
                         **({'source': source} if source else {})))
    for row in plan['copies']:
        emit(row['path'], pinned(ROOT / row['source'], row['sha256']), row['kind'], row['source'])
    for path in (plan_path, proof_path, Path(__file__).resolve()):
        emit(f'{ARCHIVE}/controllers/{path.name}', path.read_bytes(), 'frozen_export_controller')
    history = {}
    for path, expected in parent['source_snapshot_sha256'].items():
        archive = (f'{ARCHIVE}/source119/{path}' if validation['source_sha256'].get(path) == expected
                   else f'{ARCHIVE}/source117-delta/{path}')
        assert any(r['path'] == archive and r['sha256'] == expected for r in rows), path
        history[path] = dict(archive=archive, sha256=expected)
    emit(f'{ARCHIVE}/historical-source117-map.json',
         (json.dumps(dict(source_count=117, source=history, parent_manifest_sha256=plan['parent_manifest_sha256'],
                          scope='Partial recorded source coverage; unchanged bytes share source119 archive'), indent=2)+'\n').encode(),
         'historical_source_mapping')
    pair = load(DATA / 'native-str-utf8-original-pickle-paired-20261008.json')
    assert pair['terminal'] and pair['hashes_unchanged'] and pair['summary']['useful_signal']
    assert pair['pair_count'] == 7 and len(pair['raw']) == 14 and len(pair['pair_results']) == 7
    assert pair['values_trimmed'] == 0 and pair['outliers_removed'] == 0
    body_rows = []
    for item in pair['pair_results']:
        for order_index, role in enumerate(item['order']):
            run = item['runs'][role]
            assert run['passed'] and run['measurement_valid'] and run['result_signature'] == pair['result_signature']
            body_rows.append(dict(pair=item['pair_index']+1, order=order_index+1, role=role,
                                  seconds=run['original_timer_seconds_diagnostic_only'],
                                  pair_parent_over_candidate=item['ratio_parent_over_candidate'],
                                  stdout=run['stdout_log'], stdout_sha256=run['stdout_sha256']))
    emit(f'{ARCHIVE}/original-body-all14.csv', csv_bytes(list(body_rows[0]), body_rows), 'all14_body_values')
    official_rows = []
    means = {}
    for runtime, result in validation['official_results'].items():
        raw = pinned(DATA / result['output'], result['sha256'])
        suite = json.loads(raw)
        assert len(suite['benchmarks']) == 1
        bench = suite['benchmarks'][0]
        metadata = dict(suite.get('metadata', {}), **bench.get('metadata', {}))
        assert metadata['name'] == 'pickle_pure_python' and metadata['pickle_module'] == 'pickle'
        assert str(metadata['pickle_protocol']) == '5' and metadata['inner_loops'] == 20
        values = []
        for run_index, run in enumerate(bench['runs']):
            for value_index, value in enumerate(run.get('values', [])):
                values.append(value)
                official_rows.append(dict(runtime=runtime, run=run_index, value=value_index, seconds=value,
                                          source_json=result['output'], source_sha256=result['sha256']))
        assert len(values) == result['values_count'] == 20
        means[runtime] = statistics.mean(values)
        assert abs(means[runtime] - result['mean_seconds']) < 1e-14
    assert len(official_rows) == 40
    emit(f'{ARCHIVE}/official-pickle-all40.csv', csv_bytes(list(official_rows[0]), official_rows), 'all40_official_values')
    gate_info = validation['fixed_gate']
    gate = json.loads(pinned(DATA / gate_info['output'], gate_info['sha256']))
    assert gate_info['exit_code'] == 0 and gate['repeats'] == 21 and gate['warmup'] == 5 and gate['threshold'] == .10
    assert len(gate['cases']) == 11 and gate['status'] == 'pass'
    assert all(c['status'] == 'pass' for c in gate['cases'].values())
    gate_rows, gate_values = [], []
    for name, case in gate['cases'].items():
        for index, attempt in enumerate(case['attempts']):
            gate_rows.append(dict(case=name, attempt=index+1, status=attempt['status'], ratio_candidate_over_baseline=attempt['ratio'],
                                  ci_low=attempt['ratio_interval_95'][0], ci_high=attempt['ratio_interval_95'][1],
                                  baseline_median_ms=attempt['baseline_median_ms'], candidate_median_ms=attempt['candidate_median_ms']))
            arrays = {key: attempt[key] for key in ('baseline_seconds', 'candidate_seconds')}
            arrays.update({'order_balanced.'+key: values for key, values in attempt['order_balanced_seconds'].items()})
            for key, values in arrays.items():
                assert len(values) == 21
                gate_values.extend(dict(case=name, attempt=index+1, array=key, sample=i+1, seconds=value) for i,value in enumerate(values))
    emit(f'{ARCHIVE}/fixed11-gate-summary.csv', csv_bytes(list(gate_rows[0]), gate_rows), 'fixed11_summary')
    emit(f'{ARCHIVE}/fixed11-gate-all-values.csv', csv_bytes(list(gate_values[0]), gate_values), 'fixed11_all_recorded_arrays')
    report = pinned(ROOT / plan['report_path'], plan['report_sha256']).decode('utf-8')
    report += '\n## Retained numerical data\n\n'
    for title, name in [('All 14 original-body observations', 'original-body-all14.csv'), ('All 40 fresh official values', 'official-pickle-all40.csv'),
                        ('Fixed 11-case gate summary', 'fixed11-gate-summary.csv'), ('All recorded gate arrays', 'fixed11-gate-all-values.csv')]:
        report += f'- [{title}](data/native-str-utf8-checkpoint-20261008/{name})\n'
    report += '\nGate ratios below are candidate time divided by accepted baseline time; lower is faster. Every case passed the unchanged gate. These are gate checks, not whole-suite scores.\n\n'
    report += '| Case | Time ratio | 95% interval |\n|---|---:|---:|\n'
    for row in gate_rows:
        report += f"| {row['case']} | {row['ratio_candidate_over_baseline']:.6f} | [{row['ci_low']:.6f}, {row['ci_high']:.6f}] |\n"
    emit(plan['report_path'], report.encode(), 'root_report_plus_csv_links_and_gate_table', plan['report_path'])
    cpms, xms = means['cpython3147']*1000, means['xlang3']*1000
    scale = 550 / xms
    svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="960" height="320" viewBox="0 0 960 320" role="img" aria-labelledby="title desc">
<title id="title">Original pure-Python pickle, native UTF-8 checkpoint</title>
<desc id="desc">Fresh unpaired official means: CPython {cpms:.6f} milliseconds, XLang3 {xms:.6f}. XLang3 takes {xms/cpms:.6f} times CPython time. Separate paired parent-to-candidate body improvement {pair['summary']['median_pair_ratio']:.6f} times.</desc>
<rect width="960" height="320" fill="white"/><g font-family="Arial, sans-serif" fill="#182332">
<text x="28" y="36" font-size="22" font-weight="bold">Original pure-Python pickle · protocol 5</text>
<text x="28" y="63" font-size="15">Fresh official mean milliseconds · lower is faster · 20 values per runtime</text>
<text x="28" y="119" font-size="16">CPython 3.14.7</text><rect x="215" y="96" width="{cpms*scale:.4f}" height="33" fill="#237a57"/>
<text x="{225+cpms*scale:.4f}" y="119" font-size="16">{cpms:.6f} ms</text>
<text x="28" y="181" font-size="16">XLang3 candidate</text><rect x="215" y="158" width="550" height="33" fill="#ba6424"/>
<text x="775" y="181" font-size="16">{xms:.6f} ms</text>
<text x="28" y="232" font-size="16">XLang3 is {xms/cpms:.2f}× slower than CPython here; no whole-suite win.</text>
<text x="28" y="266" font-size="15">Separate XLang3 parent/candidate body screen: {pair['summary']['median_pair_ratio']:.6f}× gain, 95% CI [{pair['summary']['bootstrap95_ci'][0]:.6f}, {pair['summary']['bootstrap95_ci'][1]:.6f}].</text>
<text x="28" y="292" font-size="14">Fast-mode warnings retained. Seven paired bodies are unscored; official runs are unpaired.</text></g></svg>\n'''
    emit('doc/performance/native-str-utf8-official-pickle-20261008.svg', svg.encode(), 'official_chart')
    stage = preview / 'stage-blobs'
    stage.mkdir()
    header = 'src/internal/xlang3/builtins.h'
    raw_header = git('show', HEAD+':'+header)
    proposal = load(SCRATCH / 'native-str-utf8-encode-proposed-20261008-provenance.json')
    newline = b'\r\n' if b'\r\n' in raw_header else b'\n'
    anchor = proposal['owned_declaration_anchor'].encode()+newline
    assert raw_header.count(anchor) == 1
    declaration = proposal['owned_declaration_after_anchor'].rstrip('\r\n').replace('\r\n','\n').encode().replace(b'\n',newline)
    owned_header = raw_header.replace(anchor, anchor+declaration+newline)
    assert owned_header.replace(b'\r\n',b'\n') == (ROOT/header).read_bytes().replace(b'\r\n',b'\n')
    attrs = git('show', HEAD+':.gitattributes')
    additions = plan['owned_attribute_additions']
    assert all(line.encode() not in attrs.splitlines() for line in additions)
    owned_attrs = attrs + (b'' if attrs.endswith(b'\n') else b'\n') + b'\n' + ('\n'.join(additions)+'\n').encode()
    (stage/'builtins.h').write_bytes(owned_header)
    (stage/'gitattributes').write_bytes(owned_attrs)
    stage_plan = dict(status='owned_index_blobs_only_root_staging_pending', head=HEAD, owned_source_count=7,
                      owned_source_sha256=proposal['candidate_source_sha256'],
                      partial_owned={header: dict(blob='stage-blobs/builtins.h', sha256=sha(owned_header), head_sha256=sha(raw_header),
                                                 working_sha256=sha((ROOT/header).read_bytes()))},
                      attributes=dict(blob='stage-blobs/gitattributes', sha256=sha(owned_attrs), head_sha256=sha(attrs),
                                      working_sha256=sha((ROOT/'.gitattributes').read_bytes()), additions=additions,
                                      policy='HEAD plus exactly seven owned additions; preserve three unrelated working patterns'))
    (preview/'staging-plan.json').write_text(json.dumps(stage_plan, indent=2)+'\n', encoding='utf-8')
    for mapping in (validation['source_sha256'], validation['binaries_sha256']):
        verify_map(mapping)
    verify_map(validation['baseline_sha256'], ROOT / plan['fixed_baseline_root'])
    for row in plan['copies']:
        pinned(ROOT/row['source'], row['sha256'])
    assert dirty() == plan['tracked_dirty_sha256'] and not git('diff','--cached','--name-only')
    manifest = dict(status='ready_for_root_review_validated_trial_export', terminal=True, preview_only=True,
                    validation_sha256=plan['validation_sha256'], paired_receipt_sha256=plan['paired_receipt_sha256'],
                    source_sha256=validation['source_sha256'], binaries_sha256=validation['binaries_sha256'],
                    baseline_sha256=validation['baseline_sha256'], source_count=119, release_count=178,
                    source_coverage_limit='Partial current worktree inventory; unrelated held dirty sources are preserved; not a clean-main reproduction guarantee',
                    files=rows, file_count=len(rows), manifest_self_excluded=True,
                    staging_paths=[r['path'] for r in rows]+[MANIFEST], owned_source_count=7,
                    scratch_staging_plan='scratch/performance/native-str-utf8-checkpoint-export-preview-20261008/staging-plan.json',
                    stage_blobs_scope='Scratch preview only; not a published relative link', no_runtime_binaries_or_objects=True,
                    original_failed_receipt_preserved=True, old_full97_not_rerun=True,
                    exporter_sha256=sha(Path(__file__).read_bytes()), plan_sha256=args.plan_sha256, proof_sha256=args.proof_sha256)
    target = publication / MANIFEST
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
    (preview/'doc-paths.nul').write_bytes(('\0'.join(manifest['staging_paths'])+'\0').encode())
    print(json.dumps(dict(status=manifest['status'], preview=str(preview), files=len(rows), manifest=MANIFEST,
                          manifest_sha256=sha(target.read_bytes()), no_git_mutation=True)))

if __name__ == '__main__':
    main()
