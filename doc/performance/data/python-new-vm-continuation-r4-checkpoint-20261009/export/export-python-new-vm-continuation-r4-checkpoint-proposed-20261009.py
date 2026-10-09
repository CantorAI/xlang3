"""Root-only exact-byte checkpoint export; no tests, timing, source or Git writes."""
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
PLAN = SCRATCH / 'python-new-vm-continuation-r4-checkpoint-export-inputs-proposed-20261009.json'
PROOF = SCRATCH / 'python-new-vm-continuation-r4-checkpoint-export-provenance-proposed-20261009.json'
STEM = 'python-new-vm-continuation-r4-checkpoint-20261009'
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
    parser.add_argument('--plan-sha256', required=True)
    parser.add_argument('--proof-sha256', required=True)
    args = parser.parse_args()
    assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
    assert sys.version_info[:3] == (3, 14, 7) and sys.flags.isolated and not sys.flags.optimize
    assert Path.cwd().resolve() == ROOT
    plan, proof = read(PLAN, args.plan_sha256), read(PROOF, args.proof_sha256)
    assert proof['plan_sha256'] == args.plan_sha256 and proof['controller_sha256'] == sha(Path(__file__).read_bytes())
    assert git('rev-parse', 'HEAD').decode().strip() == plan['head']
    assert not git('diff', '--cached', '--name-only', '-z')
    index_before, dirty_before = sha(git('ls-files', '--stage', '-z')), dirty()
    attrs_before = (ROOT / '.gitattributes').read_bytes()
    app = read(ROOT / plan['application'], plan['application_sha256'])
    validation = read(ROOT / plan['validation'], plan['validation_sha256'])
    prior = read(ROOT / plan['correctness_receipt'], plan['correctness_receipt_sha256'])
    parent = read(ROOT / plan['parent_manifest'], plan['parent_manifest_sha256'])
    assert validation['status'] == 'trial_validated' and validation['terminal'] and validation['full_validated']
    assert validation['correctness_passed'] and validation['hashes_unchanged']
    assert validation['source_count'] == app['source_count'] == 132 and validation['source_sha256'] == app['source_sha256']
    assert validation['source_inventory_sha256'] == plan['application_sha256']
    assert validation['controller_sha256'] == '8b4e99e85597ce4565fe792a0b24c6e77ad47ca61ac931830721182370aefcd0'
    assert validation['fixture_counts'] == {'core':403, 'compatibility_sections':11, 'expected_failure_cases':3}
    assert validation['ctest_count'] == 9 and validation['native_api_checks'] == 2
    assert validation['hashes_before'] == validation['hashes_after']
    check_map(validation['hashes_after'], ROOT)
    release, baseline = ROOT / plan['release_root'], ROOT / plan['fixed_baseline_root']
    release_map = {Path(p).relative_to(Path(plan['release_root'])).as_posix(): h for p,h in validation['binaries_sha256'].items()}
    assert len(release_map) == 178 and tree(release) == release_map
    assert len(validation['baseline_sha256']) == 177 and tree(baseline) == validation['baseline_sha256']
    assert validation['baseline_sha256'] == parent['fixed_baseline_sha256']
    check_map(validation['source_sha256'], ROOT)
    assert validation['correctness_reused_same_candidate'] and validation['historical_correctness_receipt_sha256'] == plan['correctness_receipt_sha256']
    assert prior['status'] == 'correctness_passed_performance_pending' and prior['terminal'] and prior['correctness_passed']
    assert prior['hashes_unchanged'] and prior['hashes_before'] == prior['hashes_after'] and len(prior['phases']) == 5
    for old, row in zip(prior['phases'], validation['phases'][:5]):
        assert row['name'] == old['name'] and row['command'] == old['command'] and row['exit_code'] == 0
        assert row['passed'] and row['semantic_passed'] and not row['timed'] and not row['timing_accepted']
        assert row['executed_again'] is False and row['prior_receipt_sha256'] == plan['correctness_receipt_sha256']
        assert row['stdout_sha256'] == old['stdout_sha256'] and row['stderr_sha256'] == old['stderr_sha256']
    for row in validation['phases']:
        assert row['owned_child_cleanup_completed'] and row['post_idle_guard_passed'] and row['post_hashes_stable']
        for stream in ('stdout','stderr'):
            pin(DATA / row[stream+'_log'], row[stream+'_sha256'])
        watch = row['external_process_watch']
        pin(DATA / watch['log'], watch['sha256'])
        if row['timed']:
            assert row['passed'] and row['timing_accepted'] and row['measurement_valid'] and watch['measurement_valid']
            assert not watch['overlaps'] and not watch['scanner_errors']
    gate_info = validation['fixed_gate']
    assert gate_info['exit_code'] == 0
    gate = read(DATA / gate_info['output'], gate_info['sha256'])
    assert gate['status'] == 'pass' and (gate['repeats'],gate['warmup'],gate['threshold']) == (21,5,.10)
    assert len(gate['cases']) == 11 and all(c['status'] == 'pass' for c in gate['cases'].values())
    official, official_means = [], {}
    for runtime in ('cpython3147','xlang3'):
        result = validation['official_results']['unpickle_pure_python'][runtime]
        document = read(DATA/result['output'],result['sha256'])
        assert len(document['benchmarks']) == 1
        row = document['benchmarks'][0]
        metadata = dict(document.get('metadata',{}), **row.get('metadata',{}))
        assert metadata['name'] == 'unpickle_pure_python' and metadata['unit'] == 'second'
        assert metadata['pickle_module'] == 'pickle' and metadata['pickle_protocol'] == '5' and metadata['inner_loops'] == 20
        values = [v for run in row['runs'] for v in run.get('values',[])]
        assert len(values) == 20 and statistics.mean(values) == result['mean_seconds']
        official_means[runtime] = statistics.mean(values)
        official.extend(dict(benchmark='unpickle_pure_python',runtime=runtime,value_index=i,seconds=v) for i,v in enumerate(values))
    ratio = official_means['xlang3']/official_means['cpython3147']
    assert ratio == validation['comparison']['unpickle_pure_python']['time_xlang3_over_cpython']
    constructor = read(ROOT/plan['constructor'],plan['constructor_sha256'])
    assert constructor['terminal'] and constructor['hashes_unchanged'] and constructor['diagnostic_only'] and not constructor['official_score']
    assert constructor['source_sha256'] == app['source_sha256'] and len(constructor['phases']) == 18
    assert constructor['applied_source_sha256'] == plan['application_sha256']
    assert constructor['accepted_control_manifest_sha256'] == plan['parent_manifest_sha256']
    loop_values, medians = [], []
    for row in constructor['phases']:
        assert row['passed'] and row['timing_accepted'] and row['measurement_valid'] and row['exit_code'] == 0 and not row['timeout']
        assert row['owned_child_cleanup_completed'] and row['post_idle_guard_passed'] and row['post_hashes_stable']
        result = row['result']
        assert result['operations_per_case'] == 10000 and result['timed_operation_count'] == 40000
        assert result['hashes_unchanged'] and not result['scored'] and not result['profile_enabled'] and not result['trace_enabled']
        assert result['original_callable_identities_unchanged'] and result['setup_checks_metadata_hashes_excluded_from_timers']
        assert len(result['seconds']) == 4
        for stream in ('stdout','stderr'):
            pin(DATA/row[stream+'_log'],row[stream+'_sha256'])
        watch = row['external_process_watch']
        pin(DATA/watch['log'],watch['sha256'])
        assert watch['measurement_valid'] and not watch['overlaps'] and not watch['scanner_errors']
        for case,seconds in result['seconds'].items():
            loop_values.append(dict(round=row['round_index']+1,selection=row['selection'],case=case,operations=10000,seconds=seconds,microseconds_per_call=seconds*1e6/10000))
    assert len(loop_values) == 72
    for selection in ('cpython-python','xlang3-control','xlang3-candidate'):
        for case in ('date_class','date_saved_new','plain_class','plain_saved_new'):
            values=[r['seconds'] for r in loop_values if r['selection']==selection and r['case']==case]
            assert len(values)==6 and values==constructor['summary'][selection][case]['seconds']
            value=statistics.median(values)*1e6/10000
            assert value==constructor['summary'][selection][case]['median_microseconds_per_call']
            medians.append(dict(selection=selection,case=case,median_microseconds_per_call=value))
    eligibility=read(ROOT/plan['eligibility'],plan['eligibility_sha256'])
    assert eligibility['terminal'] and eligibility['passed'] and eligibility['hashes_unchanged'] and not eligibility['timed']
    assert eligibility['source_sha256']==app['source_sha256']
    for filename in ('pyperformance-xlang3-python-new-r4-full-fast-20261009-provenance.json','pyperformance-xlang3-python-new-r4-full-fast-r2-20261009-provenance.json'):
        refused=json.loads((DATA/filename).read_bytes())
        assert refused['terminal'] and refused['status']=='incomplete_or_invalid_full_attempt'
        assert len(refused['raw'])==1 and not refused['raw'][0]['all_97_attempted']
        assert refused['raw'][0]['exit_code'] is None and refused['raw'][0]['output_sha256'] is None
    copies={}
    for row in plan['files']:
        raw=pin(ROOT/row['source'],row['sha256'])
        assert len(raw)==row['bytes'] and row['path'].startswith('doc/performance/') and '..' not in Path(row['path']).parts
        assert row['path'] not in copies
        assert not Path(row['source']).suffix.lower() in {'.dll','.exe','.lib','.obj','.pdb'}
        if (ROOT/row['path']).exists():
            assert (ROOT/row['path']).read_bytes()==raw
        copies[row['path']]=(raw,row['kind'])
    assert len(copies)==plan['file_count']
    assert len(plan['owned_source_sha256'])==10 and app['targets_sha256']==plan['owned_source_sha256']
    for path,h in plan['owned_source_sha256'].items():
        pin(ROOT/path,h)
        if path in plan['new_owned_targets']:
            assert subprocess.run(['git','cat-file','-e','HEAD:'+path],cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.PIPE).returncode!=0
        else:
            assert (ROOT/plan['owned_input_paths'][path]).read_bytes().replace(b'\r\n',b'\n')==git('show','HEAD:'+path).replace(b'\r\n',b'\n')
    summaries,arrays=[],[]
    for case,item in gate['cases'].items():
        last=item['attempts'][-1]
        summaries.append(dict(case=case,status=item['status'],attempts=len(item['attempts']),ratio_candidate_over_baseline=last['ratio'],ci95_lower=last['ratio_interval_95'][0],ci95_upper=last['ratio_interval_95'][1]))
        for attempt_index,attempt in enumerate(item['attempts']):
            series=dict(baseline_seconds=attempt['baseline_seconds'],candidate_seconds=attempt['candidate_seconds'],**attempt['order_balanced_seconds'])
            for label,values in series.items():
                assert len(values)==21
                arrays.extend(dict(case=case,attempt_index=attempt_index,series=label,value_index=i,seconds=v) for i,v in enumerate(values))
    preview=ROOT/plan['preview_root']
    assert preview.is_relative_to(SCRATCH) and not preview.exists()
    preview.mkdir()
    publication,generated=preview/'publication',[]
    def put(path,raw,kind):
        target=publication/path
        assert target.resolve().is_relative_to(publication.resolve())
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(raw)
        generated.append(dict(path=path,sha256=sha(raw),bytes=len(raw),kind=kind))
    for path,(raw,kind) in copies.items():
        put(path,raw,kind)
    for source,h in ((Path(__file__),proof['controller_sha256']),(PLAN,args.plan_sha256),(PROOF,args.proof_sha256)):
        put(plan['archive_root']+'/export/'+source.name,pin(source,h),'frozen_export_metadata')
    put('doc/performance/data/'+STEM+'-constructor-values.csv',csv_bytes(['round','selection','case','operations','seconds','microseconds_per_call'],loop_values),'all72_artificial_values')
    put('doc/performance/data/'+STEM+'-constructor-medians.csv',csv_bytes(['selection','case','median_microseconds_per_call'],medians),'artificial_medians')
    put('doc/performance/data/'+STEM+'-official-values.csv',csv_bytes(['benchmark','runtime','value_index','seconds'],official),'all40_official_values')
    put('doc/performance/data/'+STEM+'-fixed-gate-summary.csv',csv_bytes(list(summaries[0]),summaries),'default11_gate_summary')
    put('doc/performance/data/'+STEM+'-fixed-gate-values.csv',csv_bytes(['case','attempt_index','series','value_index','seconds'],arrays),'all_default_gate_arrays')
    text='\n| Artificial case (µs/call, lower is faster) | CP explicit Python | Previous X | Candidate X | Previous X / candidate |\n|---|---:|---:|---:|---:|\n'
    summary=constructor['summary']
    for case in ('date_class','date_saved_new','plain_class','plain_saved_new'):
        cp=summary['cpython-python'][case]['median_microseconds_per_call']
        old=summary['xlang3-control'][case]['median_microseconds_per_call']
        new=summary['xlang3-candidate'][case]['median_microseconds_per_call']
        text+=f'| {case} | {cp:.6f} | {old:.6f} | {new:.6f} | {old/new:.6f}× |\n'
    text+='\nThe previous-X/candidate ratios above belong only to the balanced artificial diagnostic.\n\n| Original official case | CPython 3.14.7 mean (ms) | Candidate X mean (ms) | X / CP elapsed time |\n|---|---:|---:|---:|\n'
    text+=f"| unpickle_pure_python | {official_means['cpython3147']*1000:.6f} | {official_means['xlang3']*1000:.6f} | {ratio:.6f}× |\n"
    text+=f'\n[All 72 constructor values](data/{STEM}-constructor-values.csv), [constructor medians](data/{STEM}-constructor-medians.csv), [all 40 official values](data/{STEM}-official-values.csv), [fixed 11 gate summary](data/{STEM}-fixed-gate-summary.csv), [all gate arrays](data/{STEM}-fixed-gate-values.csv).\n'
    report=pin(ROOT/plan['report_template'],plan['report_template_sha256']).decode()
    assert report.count('<!-- ACTUAL_TABLES -->')==1
    put('doc/performance/'+STEM+'.md',report.replace('<!-- ACTUAL_TABLES -->',text).encode(),'checkpoint_report')
    attrs_head=git('show','HEAD:.gitattributes')
    attrs=attrs_head+(b'' if attrs_head.endswith(b'\n') else b'\n')+b'\n'+('\n'.join(plan['owned_attribute_additions'])+'\n').encode()
    stage=preview/'staging';stage.mkdir()
    (stage/'gitattributes-head-plus-owned').write_bytes(attrs)
    (stage/'owned-attributes-additions.txt').write_bytes(('\n'.join(plan['owned_attribute_additions'])+'\n').encode())
    staging=dict(scope='Scratch plan only; root stages ten owned sources normalized by Git, exact doc paths and HEAD attributes plus owned additions.',owned_source_sha256=plan['owned_source_sha256'],owned_git_blob_sha1={p:git('hash-object','--path='+p,str(ROOT/p)).decode().strip() for p in plan['owned_source_sha256']},new_owned_targets=plan['new_owned_targets'],owned_attribute_additions=plan['owned_attribute_additions'],attrs_head_sha256=sha(attrs_head),attrs_candidate_sha256=sha(attrs),working_attrs_sha256=sha(attrs_before))
    (preview/'staging-plan.json').write_text(json.dumps(staging,indent=2)+'\n',encoding='utf-8')
    assert len(generated)==len({r['path'] for r in generated})
    manifest=dict(status='validated_checkpoint_full97_pending',terminal=True,full_validated=True,whole97_rerun=False,whole97_status=plan['whole97_status'],files=list(generated),source_count=132,binary_count=178,fixed_baseline_count=177,owned_source_sha256=plan['owned_source_sha256'],correctness_passed=True,fixed_gate_passed=True,validation_sha256=plan['validation_sha256'],constructor_sha256=plan['constructor_sha256'],eligibility_sha256=plan['eligibility_sha256'],plan_sha256=args.plan_sha256,proof_sha256=args.proof_sha256,controller_sha256=proof['controller_sha256'],staging_metadata_scope='Scratch preview only; no published relative staging link',no_official_candidate_gain_or_whole_suite_gain_claim=True)
    put('doc/performance/data/'+STEM+'-publication.json',(json.dumps(manifest,indent=2)+'\n').encode(),'standalone_publication_manifest')
    assert attrs_before==(ROOT/'.gitattributes').read_bytes() and dirty_before==dirty() and index_before==sha(git('ls-files','--stage','-z'))
    check_map(validation['source_sha256'],ROOT)
    assert tree(release)==release_map and tree(baseline)==validation['baseline_sha256']
    for row in plan['files']:
        pin(ROOT/row['source'],row['sha256'])
    print(json.dumps(dict(status='scratch_preview_exported',preview=str(preview),files=len(generated),live_files_unchanged=True)))

if __name__=='__main__':
    main()
