"""Root-only file export after the R5 default gate; no tests, timings or Git writes."""
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
PLAN = SCRATCH / 'lambda-eager-comprehension-capture-r5-checkpoint-export-inputs-proposed-20261009.json'
PROOF = SCRATCH / 'lambda-eager-comprehension-capture-r5-checkpoint-export-provenance-proposed-20261009.json'
STEM = 'lambda-eager-comprehension-capture-r5-checkpoint-20261009'
sha = lambda raw: hashlib.sha256(raw).hexdigest()

def pin(path, expected):
    raw = Path(path).read_bytes()
    assert sha(raw) == expected, str(path)
    return raw

def read(path, expected):
    return json.loads(pin(path, expected))

def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT)

def tree(root):
    assert not root.is_symlink()
    result = {}
    for path in root.rglob('*'):
        assert not path.is_symlink(), str(path)
        if path.is_file():
            result[path.relative_to(root).as_posix()] = sha(path.read_bytes())
    return result

def check_map(mapping, base):
    for path, expected in mapping.items():
        pin(base / path, expected)

def dirty():
    return {p: sha((ROOT / p).read_bytes()) for p in
            git('diff', 'HEAD', '--name-only', '-z').decode().split('\0') if p}

def csv_bytes(fields, rows):
    text = io.StringIO(newline='')
    writer = csv.DictWriter(text, fieldnames=fields, lineterminator='\n')
    writer.writeheader()
    writer.writerows(rows)
    return text.getvalue().encode()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('plan', 'proof', 'validation'):
        if name == 'validation':
            parser.add_argument('--validation', type=Path, required=True)
        parser.add_argument('--' + name + '-sha256', required=True)
    args = parser.parse_args()
    assert sys.version_info[:3] == (3, 14, 7) and sys.flags.optimize == 0
    assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
    assert Path.cwd().resolve() == ROOT
    plan, proof = read(PLAN, args.plan_sha256), read(PROOF, args.proof_sha256)
    assert proof['plan_sha256'] == args.plan_sha256
    assert proof['controller_sha256'] == sha(Path(__file__).read_bytes())
    assert git('rev-parse', 'HEAD').decode().strip() == plan['head']
    assert not git('diff', '--cached', '--name-only', '-z')
    index_before, dirty_before = sha(git('ls-files', '--stage', '-z')), dirty()
    attrs_before = (ROOT / '.gitattributes').read_bytes()
    current = read(ROOT / plan['application'], plan['application_sha256'])
    validation = read(args.validation, args.validation_sha256)
    prior = read(ROOT / plan['correctness_receipt'], plan['correctness_receipt_sha256'])
    assert prior['status'] == 'correctness_passed_performance_pending' and prior['terminal']
    assert prior['correctness_passed'] and prior['hashes_unchanged']
    assert validation['terminal'] and validation['status'] in ('trial_validated', 'trial_validation_failed')
    assert validation['correctness_passed'] and validation['hashes_unchanged']
    assert validation['controller_sha256'] == plan['validation_controller_sha256']
    assert validation['source_count'] == 128 and validation['source_sha256'] == current['source_sha256']
    assert validation['source_inventory_sha256'] == plan['application_sha256']
    assert validation['candidate_binary_sha256'] == prior['candidate_binary_sha256']
    assert validation['binaries_sha256'] == prior['binaries_sha256']
    assert validation['baseline_sha256'] == prior['baseline_sha256']
    assert validation['fixture_counts'] == {'core': 401, 'compatibility_sections': 11, 'expected_failure_cases': 3}
    assert validation['ctest_count'] == 9 and validation['native_api_checks'] == 2
    assert validation['ir_eligibility_receipt_sha256'] == plan['ir_eligibility_sha256']
    assert validation['hashes_before'] == validation['hashes_after']
    check_map(validation['hashes_after'], ROOT)
    release, baseline = ROOT / plan['release_root'], ROOT / plan['fixed_baseline_root']
    assert len(validation['binaries_sha256']) == 178 and len(validation['baseline_sha256']) == 177
    assert tree(release) == {Path(p).relative_to(Path(plan['release_root'])).as_posix(): h
                             for p, h in validation['binaries_sha256'].items()}
    assert tree(baseline) == validation['baseline_sha256']
    check_map(validation['source_sha256'], ROOT)
    assert validation['correctness_reused_same_candidate']
    assert validation['historical_correctness_receipt_sha256'] == plan['correctness_receipt_sha256']
    assert len(prior['phases']) == 5
    for old, row in zip(prior['phases'], validation['phases'][:5]):
        assert row['name'] == old['name'] and row['command'] == old['command']
        assert row['semantic_passed'] and row['passed'] and row['exit_code'] == 0 and not row['timed']
        assert row['executed_again'] is False and row['prior_receipt_sha256'] == plan['correctness_receipt_sha256']
        assert row['stdout_sha256'] == old['stdout_sha256'] and row['stderr_sha256'] == old['stderr_sha256']
    copied = {}
    def queue(source, path, expected, kind):
        source = Path(source)
        raw = pin(source, expected)
        assert path.startswith('doc/performance/') and '..' not in Path(path).parts
        if path in copied:
            assert copied[path]['sha256'] == expected
        else:
            copied[path] = dict(source=str(source), path=path, sha256=expected, bytes=len(raw), kind=kind)
        if (ROOT / path).exists():
            assert (ROOT / path).read_bytes() == raw
    for row in plan['files']:
        queue(ROOT / row['source'], row['path'], row['sha256'], row['kind'])
    queue(args.validation, 'doc/performance/data/' + args.validation.name, args.validation_sha256, 'terminal_validation')
    for row in validation['phases']:
        assert row['owned_child_cleanup_completed'] and row['post_idle_guard_passed'] and row['post_hashes_stable']
        for stream in ('stdout', 'stderr'):
            queue(DATA / row[stream + '_log'], 'doc/performance/data/' + row[stream + '_log'],
                  row[stream + '_sha256'], 'raw_stream')
        watch = row['external_process_watch']
        queue(DATA / watch['log'], 'doc/performance/data/' + watch['log'], watch['sha256'], 'raw_watch')
        if row['timed']:
            assert row['measurement_valid'] and watch['measurement_valid']
            assert not watch['overlaps'] and not watch['scanner_errors']
        else:
            assert row['semantic_passed'] and not row['timing_accepted']
    gate_info = validation['fixed_gate']
    assert gate_info['exit_code'] == 0
    gate = read(DATA / gate_info['output'], gate_info['sha256'])
    assert gate['status'] == 'pass' and (gate['repeats'], gate['warmup'], gate['threshold']) == (21, 5, .10)
    assert len(gate['cases']) == 11 and all(c['status'] == 'pass' for c in gate['cases'].values())
    gate_phase = next(r for r in validation['phases'] if r['name'] == 'fixed-gate')
    assert gate_phase['passed'] and gate_phase['timing_accepted'] and gate_phase['exit_code'] == 0
    queue(DATA / gate_info['output'], 'doc/performance/data/' + gate_info['output'], gate_info['sha256'], 'default_gate')
    summaries, arrays = [], []
    for name, case in gate['cases'].items():
        last = case['attempts'][-1]
        summaries.append(dict(case=name, status=case['status'], attempts=len(case['attempts']),
                              ratio_candidate_over_baseline=last['ratio'],
                              ci95_lower=last['ratio_interval_95'][0], ci95_upper=last['ratio_interval_95'][1]))
        for attempt_index, attempt in enumerate(case['attempts']):
            series = dict(baseline_seconds=attempt['baseline_seconds'], candidate_seconds=attempt['candidate_seconds'],
                          **attempt['order_balanced_seconds'])
            for label, values in series.items():
                assert len(values) == 21
                arrays.extend(dict(case=name, attempt_index=attempt_index, series=label, value_index=i, seconds=v)
                              for i, v in enumerate(values))
    official, outcomes, bars = [], [], []
    for benchmark in ('sqlalchemy_declarative', 'sqlalchemy_imperative'):
        row = next(r for r in validation['phases'] if r['name'] == 'official-xlang3-' + benchmark)
        attempt = validation['official_attempts'][row['name']]
        if attempt['sha256'] is not None:
            queue(DATA / attempt['output'], 'doc/performance/data/' + attempt['output'], attempt['sha256'], 'official_attempt_output')
        for runtime in ('cpython3147', 'xlang3'):
            result = validation['official_results'][benchmark].get(runtime)
            if result is None:
                assert runtime == 'xlang3' and not row['passed'] and not row['timing_accepted'] and not attempt['complete']
                outcomes.append(dict(benchmark=benchmark, runtime=runtime, complete=False, exit_code=row['exit_code'],
                                     mean_seconds='', time_x_over_cp='', stderr_log=row['stderr_log']))
                continue
            raw = read(DATA / result['output'], result['sha256'])
            assert len(raw['benchmarks']) == 1
            assert raw['benchmarks'][0].get('metadata', {}).get('name', raw.get('metadata', {}).get('name')) == benchmark
            values = [v for run in raw['benchmarks'][0]['runs'] for v in run.get('values', [])]
            assert len(values) == 20 and statistics.mean(values) == result['mean_seconds']
            if runtime == 'cpython3147':
                assert result['executed_again'] is False and result['prior_receipt_sha256'] == plan['historical_cp_receipt_sha256']
            else:
                assert row['passed'] and row['timing_accepted'] and row['exit_code'] == 0 and attempt['complete']
                assert attempt['sha256'] == result['sha256']
                cp_mean = validation['official_results'][benchmark]['cpython3147']['mean_seconds']
                bars.append((benchmark, cp_mean, result['mean_seconds']))
            queue(DATA / result['output'], 'doc/performance/data/' + result['output'], result['sha256'], 'official_values')
            official.extend(dict(benchmark=benchmark, runtime=runtime, value_index=i, seconds=v) for i, v in enumerate(values))
            outcomes.append(dict(benchmark=benchmark, runtime=runtime, complete=True, exit_code=0,
                                 mean_seconds=result['mean_seconds'], time_x_over_cp=(result['mean_seconds']/cp_mean if runtime == 'xlang3' else ''), stderr_log=''))
    for path, expected in validation.get('partial_evidence_sha256', {}).items():
        queue(DATA / path, 'doc/performance/data/' + path, expected, 'failed_partial')
    assert validation['full_validated'] == (validation['status'] == 'trial_validated') == (len(bars) == 2)
    for path, expected in plan['owned_source_sha256'].items():
        pin(ROOT / path, expected)
        if path in plan['new_owned_targets']:
            assert subprocess.run(['git', 'cat-file', '-e', 'HEAD:' + path], cwd=ROOT,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE).returncode != 0
        else:
            assert (ROOT / plan['owned_input_paths'][path]).read_bytes().replace(b'\r\n', b'\n') == git('show', 'HEAD:' + path).replace(b'\r\n', b'\n')
    preview = ROOT / plan['preview_root']
    assert preview.is_relative_to(SCRATCH) and not preview.exists()
    preview.mkdir()
    publication, generated = preview / 'publication', []
    def put(path, raw, kind):
        target = publication / path
        assert target.resolve().is_relative_to(publication.resolve())
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(raw)
        generated.append(dict(path=path, sha256=sha(raw), bytes=len(raw), kind=kind))
    for row in copied.values():
        put(row['path'], pin(row['source'], row['sha256']), row['kind'])
    for source, expected in ((Path(__file__), proof['controller_sha256']), (PLAN, args.plan_sha256), (PROOF, args.proof_sha256)):
        put(plan['archive_root'] + '/export/' + source.name, pin(source, expected), 'frozen_export_metadata')
    put('doc/performance/data/' + STEM + '-official-values.csv', csv_bytes(['benchmark','runtime','value_index','seconds'], official), 'all_completed_official_values')
    put('doc/performance/data/' + STEM + '-official-outcomes.csv', csv_bytes(list(outcomes[0]), outcomes), 'actual_official_outcomes')
    put('doc/performance/data/' + STEM + '-fixed-gate-summary.csv', csv_bytes(list(summaries[0]), summaries), 'default_gate_summary')
    put('doc/performance/data/' + STEM + '-fixed-gate-values.csv', csv_bytes(['case','attempt_index','series','value_index','seconds'], arrays), 'all_gate_arrays')
    results = '\n\nDefault gate: all 11 cases passed with unchanged 21/5/0.10 settings.\n\n| Case | Runtime | Complete | Mean (ms) | X/CP time |\n|---|---|---:|---:|---:|\n'
    for row in outcomes:
        mean = format(row['mean_seconds']*1000, '.6f') if row['complete'] else 'no score'
        ratio = format(row['time_x_over_cp'], '.6f') if row['time_x_over_cp'] != '' else ''
        results += f"| {row['benchmark']} | {row['runtime']} | {row['complete']} | {mean} | {ratio} |\n"
    results += f'\n[Official values](data/{STEM}-official-values.csv), [outcomes and failures](data/{STEM}-official-outcomes.csv), [default-gate summary](data/{STEM}-fixed-gate-summary.csv), [all gate arrays](data/{STEM}-fixed-gate-values.csv).\n'
    if bars:
        limit = max(max(cp,x) for _,cp,x in bars)*1000*1.05
        height = 110+len(bars)*110
        svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="1000" height="{height}" viewBox="0 0 1000 {height}"><rect width="1000" height="{height}" fill="white"/><g font-family="Arial" fill="#172033"><text x="25" y="25" font-size="18">Original SQLAlchemy elapsed time — lower is faster</text><text x="25" y="48" font-size="12">Fresh XLang / saved CPython 3.14.7; historical unpaired fast results, all 20 values retained</text>']
        for i,(name,cp,x) in enumerate(bars):
            y=80+i*110
            svg.append(f'<text x="25" y="{y}" font-size="14">{name}</text>')
            for j,(label,value,color) in enumerate((('CP',cp,'#5275aa'),('XLang',x,'#d07939'))):
                sy=y+15+j*30
                svg.append(f'<text x="25" y="{sy+16}" font-size="13">{label}</text><rect x="100" y="{sy}" width="{value*1000/limit*690:.4f}" height="22" fill="{color}"/><text x="805" y="{sy+16}" font-size="13">{value*1000:.3f} ms</text>')
        svg.append('</g></svg>')
        put('doc/performance/' + STEM + '.svg', '\n'.join(svg).encode(), 'completed_rows_only_elapsed_time_chart')
        results += f'\n![Completed official rows only]({STEM}.svg)\n'
    report = pin(ROOT / plan['report_template'], plan['report_template_sha256']).decode()
    put('doc/performance/' + STEM + '.md', report.replace('<!-- TERMINAL_RESULTS -->', results).encode(), 'actual_terminal_report')
    attrs_head = git('show', 'HEAD:.gitattributes')
    attrs = attrs_head + (b'' if attrs_head.endswith(b'\n') else b'\n') + b'\n' + ('\n'.join(plan['owned_attribute_additions'])+'\n').encode()
    stage = preview / 'staging'
    stage.mkdir()
    (stage/'gitattributes-head-plus-owned').write_bytes(attrs)
    staging = dict(scope='Scratch blobs only; root stages exact six owned targets plus HEAD attributes and owned additions.',
                   owned_source_sha256=plan['owned_source_sha256'], new_owned_targets=plan['new_owned_targets'],
                   attrs_head_sha256=sha(attrs_head), attrs_candidate_sha256=sha(attrs),
                   working_attrs_sha256=sha(attrs_before), owned_attribute_additions=plan['owned_attribute_additions'])
    (preview/'staging-plan.json').write_text(json.dumps(staging,indent=2)+'\n', encoding='utf-8')
    assert len(generated) == len({r['path'] for r in generated})
    manifest = dict(status='compiler_correctness_and_default_gate_passed_official_attempts_retained', terminal=True,
                    full_validated=validation['full_validated'], whole97_rerun=False, files=generated,
                    owned_source_sha256=plan['owned_source_sha256'], source_count=128, binary_count=178,
                    fixed_baseline_count=177, correctness_passed=True, fixed_gate_passed=True,
                    validation_sha256=args.validation_sha256, validation=str(args.validation),
                    correctness_receipt_sha256=plan['correctness_receipt_sha256'], plan_sha256=args.plan_sha256,
                    proof_sha256=args.proof_sha256, controller_sha256=proof['controller_sha256'],
                    staging_metadata_scope='Scratch preview only; no published relative staging link.',
                    historical_cpython_reference=validation['historical_cpython_reference'])
    put('doc/performance/data/' + STEM + '-publication.json', (json.dumps(manifest,indent=2)+'\n').encode(), 'publication_manifest')
    assert attrs_before == (ROOT/'.gitattributes').read_bytes()
    assert dirty_before == dirty() and index_before == sha(git('ls-files','--stage','-z'))
    check_map(validation['source_sha256'], ROOT)
    check_map(validation['binaries_sha256'], ROOT)
    assert tree(baseline) == validation['baseline_sha256']
    for row in copied.values():
        pin(row['source'],row['sha256'])
    print(json.dumps(dict(status='scratch_preview_exported',preview=str(preview),file_count=len(generated),live_files_unchanged=True)))

if __name__ == '__main__':
    main()
