"""Fixed S8/layout callback or original pprint pairs; root launches, unscored."""
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import random
import re
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
CONTROL = ROOT / 'build-repro/controls/sorted-exact-int-s8-validated-checkpoint-20261008'
CONTROL_SHA = '67449b0b9ecd5a1b4c9669acef85d8b89eb28c7b31df60b7a4653ad5a7c79b56'
FOCUS_CONTROLLER = ROOT / 'scratch/performance/check-immutable-entry-layout-focused-r2-20261008.py'
FOCUS_CONTROLLER_SHA = '4b5d28ec1fd6ec59ebfbc9b7baa40f2c3b879e5f332da0cacf7ef0806fb42517'
BASE = DATA / 'sorted-exact-int-s8-registered-source-20261008.json'
BASE_SHA = 'f20c304e32b0874c929d22dc72018cbd6d5fd9d124844c1d66d8edcc6a231556'
VALIDATION = DATA / 'sorted-exact-int-s8-full-validation-20261008.json'
VALIDATION_SHA = 'd4b028b1255f23b11e664bd9e6778b808a7d160d3628e17f39710faa1900d022'
PROOF = ROOT / 'scratch/performance/native-python-entry-immutable-layout-proposed-20261008-provenance.json'
PROOF_SHA = 'bde7e4fa9b891e60eef8886107675df34c0e6905b94259c59a6c2574b7e2e797'
CALLBACK = ROOT / 'benchmarks/diagnostics/python_callback_boundary.py'
CALLBACK_SHA = 'f76136594ac8e64e5fb0d71a47eff3af286d5e12178a8acbaa7fe4c43a6ef84a'
PPRINT_CHILD = ROOT / 'scratch/performance/pprint-original-safe-repr-r2-diagnostic-20261008.py'
PPRINT_CHILD_SHA = '5d57809bfc7437ccb6eef30d54c34b78f87824d2b01644959717cd36a8ea437c'
PPRINT_BENCHMARK = CP.parent / 'Lib/site-packages/pyperformance/data-files/benchmarks/bm_pprint/run_benchmark.py'
PPRINT_BENCHMARK_SHA = '07ef57201c7919aedf9e340c7fb1561a40dfee698818c9694930f3e427091d6d'
PPRINT_SHA = 'c29eb77af95120a7e9b107b6dc3cf093fcbe37b22aa27441cf58cacbeb56904e'
HOOK = ROOT / 'benchmarks/diagnostics/pyperf_compat/sitecustomize.py'
HOOK_SHA = '2ffc217fe20497156bfcb1cebc7fb87203f33c6d4d3dbb6ecbcbcf91ea4cb317'
DEPENDENCIES = DATA / 'pyperformance-cpython3147-live-eval-full-fast-20261007-provenance.json'
DEPENDENCIES_SHA = '3d6c8cb2c72c786583c382b835e8e411f56ca285963bc2835f757a13f399f85c'
WATCH = ROOT / 'scratch/performance/validate-call-ex-cross-activation-constructor-resume-r3-20261008.py'
WATCH_SHA = '50007db5cc6adc2e54012fdfbd48514699513f990906d4a508582ba73d6ae0bf'
CASES = ('loop', 'small_calls', 'branch_calls', 'sort_plain', 'sort_callback')
ORDERS = [('control', 'candidate') if i % 2 == 0 else ('candidate', 'control') for i in range(7)]
SIGNATURE = dict(tuple_length=3, text_length=4200000, readable=True, recursive=False,
    text_utf8_sha256='15c269515f4a6ef7ae2566cccacd6b5c6613494a11c0d730e2e6c83c1010de6e')
SHA = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
READ = lambda p: json.loads(Path(p).read_bytes())

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('callback-pairs', 'pprint-pairs'), required=True)
    parser.add_argument('--source-inventory', type=Path, required=True)
    parser.add_argument('--source-inventory-sha256', required=True)
    parser.add_argument('--focused-receipt', type=Path, required=True)
    parser.add_argument('--focused-receipt-sha256', required=True)
    parser.add_argument('--prefix', required=True)
    args = parser.parse_args()
    if sys.implementation.name != 'cpython' or sys.version_info[:3] != (3,14,7) or sys.flags.optimize:
        raise RuntimeError('Use unoptimized CPython 3.14.7 only')
    assert Path(sys.executable).resolve() == CP.resolve()
    assert re.fullmatch(r'[a-z0-9-]+', args.prefix) and not any(DATA.glob(args.prefix + '*'))
    pins = {}
    def pin(path, expected=None):
        path = Path(path).resolve(strict=True); value = SHA(path)
        assert expected is None or value == expected, str(path)
        assert str(path) not in pins or pins[str(path)] == value
        pins[str(path)] = value; return value
    for path, value in [(args.source_inventory,args.source_inventory_sha256), (args.focused_receipt,args.focused_receipt_sha256),
        (FOCUS_CONTROLLER,FOCUS_CONTROLLER_SHA), (BASE,BASE_SHA), (VALIDATION,VALIDATION_SHA), (PROOF,PROOF_SHA),
        (CONTROL/'preserved-release-provenance.json',CONTROL_SHA), (WATCH,WATCH_SHA), (HOOK,HOOK_SHA)]: pin(path,value)
    spec = importlib.util.spec_from_file_location('immutable_layout_focus_constants', FOCUS_CONTROLLER)
    constants = importlib.util.module_from_spec(spec); spec.loader.exec_module(constants)
    inventory, focus, base, proof, control = READ(args.source_inventory), READ(args.focused_receipt), READ(BASE), READ(PROOF), READ(CONTROL/'preserved-release-provenance.json')
    sources = dict(base['source_sha256'], **proof['candidate_source_sha256'])
    assert inventory['source_sha256'] == sources and len(sources) == 111
    assert control['file_count'] == len(control['files_sha256']) == 178 and control['source_count'] == 110
    assert control['source_snapshot_sha256'] == base['source_sha256']
    prior = READ(VALIDATION)
    assert prior['terminal'] and prior['status'] == 'validated' and prior['full_validated'] and prior['hashes_unchanged']
    assert focus['terminal'] and focus['status'] == 'targeted_correctness_passed' and focus['hashes_unchanged']
    assert focus['controller_sha256'] == FOCUS_CONTROLLER_SHA and focus['source_inventory_sha256'] == args.source_inventory_sha256
    assert focus['source_sha256'] == sources and focus['hashes_before'] == focus['hashes_after']
    assert [r['name'] for r in focus['phases']] == [name for name, _ in constants.PHASES]
    for row, (_, case) in zip(focus['phases'], constants.PHASES):
        command = [str(RELEASE / ('xlang3.exe' if case else 'xlang3_interpreter_tests.exe'))]
        if case: command.append(str(ROOT / ('tests/fixtures/core/' + case + '.py')))
        assert row['command'] == command and row['passed'] and row['exit_code'] == 0 and not row['timeout']
        if case: assert row['output_matches_expected']
        for stream in ('stdout','stderr'): pin(DATA/row[stream+'_log'],row[stream+'_sha256'])
    for path, value in focus['hashes_before'].items(): pin(path,value)
    release_map = lambda: {p.relative_to(ROOT).as_posix(): SHA(p) for p in sorted(RELEASE.rglob('*')) if p.is_file()}
    binaries = release_map(); assert len(binaries) == 178 and binaries == focus['binaries_sha256']
    for path, value in {**sources, **binaries}.items(): pin(ROOT/path,value)
    for base_path, values in [(CONTROL,control['files_sha256']), (CONTROL/'source-snapshot',control['source_snapshot_sha256'])]:
        for path,value in values.items(): pin(base_path/path,value)
    for path,value in [(CP,'4942b86a6597e5aee0128daa00050ed79bc21f6e709a78eb19cbfeb0c2f39ac9'),
        (CP.parent/'python314.dll','0f9857ffdfe010fe6b99328d58c2e3c7472ce75f336bf9c2ad9bd5bca3bce700')]: pin(path,value)
    environment = os.environ.copy()
    for name in ('PYTHONOPTIMIZE','PYTHONPATH','PYTHONPYCACHEPREFIX','PYTHONIOENCODING','_NT_SYMBOL_PATH','_NT_ALT_SYMBOL_PATH'): environment.pop(name,None)
    assert not environment.get('XLANG3_VM_OPCODE_TIMING')
    environment.update(XLANG3_PYTHON_LIB=str(CP.parent/'Lib'), PYTHONPATH=str(HOOK.parent), PYTHONIOENCODING='utf-8', PYTHONUNBUFFERED='1')
    if args.mode == 'callback-pairs': pin(CALLBACK,CALLBACK_SHA)
    else:
        for path,value in [(PPRINT_CHILD,PPRINT_CHILD_SHA),(PPRINT_BENCHMARK,PPRINT_BENCHMARK_SHA),(CP.parent/'Lib/pprint.py',PPRINT_SHA), (DEPENDENCIES,DEPENDENCIES_SHA)]: pin(path,value)
        dependency = READ(DEPENDENCIES); site = Path(dependency['dependency_site']).resolve(strict=True)
        metadata = {p.relative_to(site).as_posix():pin(p) for p in sorted(site.glob('*.dist-info/METADATA'))}
        assert metadata == {p.replace('\\','/'):v for p,v in dependency['dependency_metadata_sha256'].items()}
        for package in (CP.parent/'Lib/site-packages/pyperf',site/'pyperf'):
            for path in sorted(package.rglob('*.py')): pin(path)
        environment['PYTHONPATH'] = os.pathsep.join((str(HOOK.parent),str(site)))
    pin(__file__)
    record = dict(status='running_unscored_layout_pairs', terminal=False, scored=False, acceptance=False, full_validated=False,
        mode=args.mode, source_inventory_sha256=args.source_inventory_sha256, source_sha256=sources,
        binaries_sha256=binaries, focused_receipt_sha256=args.focused_receipt_sha256, control_manifest_sha256=CONTROL_SHA,
        controller_sha256=SHA(__file__), pair_count=7, pair_order=ORDERS, values_trimmed=0, outliers_removed=0,
        result_signature=SIGNATURE if args.mode=='pprint-pairs' else None,
        decision_rule=dict(selected_case='pprint_original_body' if args.mode=='pprint-pairs' else 'sort_callback',
            minimum_median_ratio=1.02, minimum_lower_ci=1.0, bootstrap_resamples=50000, bootstrap_seed=20261008,
            strict_greater_than=True, negative_control_median_floor=1/1.05),
        total_pair_children=14, cpython_parity_children=1 if args.mode=='pprint-pairs' else 0,
        scope='Diagnostic signal only; neutral pure pickle does not reject a callback/pprint gain; full gate/official required',
        hashes_before=dict(pins), child_environment={name:environment[name] for name in
            ('XLANG3_PYTHON_LIB','PYTHONPATH','PYTHONIOENCODING','PYTHONUNBUFFERED')}, started_utc=datetime.now(timezone.utc).isoformat(),
        pair_results=[], raw=[], idle_guards=[])
    if args.mode=='callback-pairs':
        record.update(total_timed_case_invocations=210, operations_per_case=50000,
            warmup_case_invocations_per_child=5, timed_samples_per_case_per_child=3)
    else:
        record.update(input_length=100000, repeat_per_child=1, profile_enabled=False,
            total_original_body_invocations=15)
    output = DATA/(args.prefix+'.json')
    def save(): output.write_bytes((json.dumps(record,indent=2)+'\n').encode('utf-8'))
    def control_maps():
        snapshot=CONTROL/'source-snapshot'
        files={p.relative_to(CONTROL).as_posix():SHA(p) for p in CONTROL.rglob('*') if p.is_file() and p!=CONTROL/'preserved-release-provenance.json' and not p.is_relative_to(snapshot)}
        snapshots={p.relative_to(snapshot).as_posix():SHA(p) for p in snapshot.rglob('*') if p.is_file()}
        return files,snapshots
    def stable(): return release_map()==binaries and control_maps()==(control['files_sha256'],control['source_snapshot_sha256']) and all(Path(p).is_file() and SHA(p)==h for p,h in pins.items())
    def idle(label):
        rows=json.loads(subprocess.check_output(['powershell','-NoProfile','-Command','Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress']).decode('utf-8-sig') or '[]')
        if isinstance(rows,dict): rows=[rows]
        busy=[r for r in rows if r['ProcessId']!=os.getpid() and (r['Name'].lower().startswith(('python','xlang3')) or r['Name'].lower() in {'cl.exe','link.exe','ninja.exe','cmake.exe','ctest.exe','msbuild.exe','nmake.exe','clang-cl.exe','lld-link.exe'})]
        record['idle_guards'].append(dict(phase=label,busy=busy)); save(); assert not busy,busy
    watcher_spec=importlib.util.spec_from_file_location('immutable_layout_timing_watch',WATCH)
    watcher=importlib.util.module_from_spec(watcher_spec); watcher_spec.loader.exec_module(watcher)
    def run(role,exe,label):
        idle('before-'+label); assert stable()
        cache=ROOT/'scratch/performance'/('pycache-'+args.prefix+'-'+label); assert not cache.exists()
        command=[str(exe),str(CALLBACK)] if args.mode=='callback-pairs' else [str(exe),str(PPRINT_CHILD),'--benchmark-script',str(PPRINT_BENCHMARK)]
        stdout=DATA/(args.prefix+'-'+label+'.stdout.log'); stderr=DATA/(args.prefix+'-'+label+'.stderr.log')
        row=dict(runtime=role,command=command,timeout=False,passed=False,stdout_log=stdout.name,stderr_log=stderr.name)
        record['raw'].append(row); save(); child=None; finish=watcher.start_timing_process_watch(args.prefix,label,row)
        try:
            with stdout.open('xb') as out,stderr.open('xb') as err:
                child=subprocess.Popen(command,cwd=ROOT,env=dict(environment,PYTHONPYCACHEPREFIX=str(cache)),stdin=subprocess.DEVNULL,stdout=out,stderr=err,creationflags=subprocess.CREATE_NO_WINDOW)
                row['pid']=child.pid; save()
                try: row['exit_code']=child.wait(timeout=120 if args.mode=='callback-pairs' else 300)
                except subprocess.TimeoutExpired:
                    row['timeout']=True
                    subprocess.run(['taskkill','/F','/T','/PID',str(child.pid)],capture_output=True,timeout=15)
                    if child.poll() is None: child.kill()
                    row['exit_code']=child.wait(timeout=15)
            assert row['exit_code']==0 and not row['timeout'] and stderr.read_bytes()==b''
            if args.mode=='callback-pairs':
                times={case:[] for case in CASES}; indices={case:[] for case in CASES}
                lines=stdout.read_text(encoding='utf-8-sig').splitlines(); assert len(lines)==15
                for line in lines:
                    marker,name,sample,count,value=line.split(); assert marker=='callback_boundary' and name in times and int(count)==50000
                    elapsed=float(value); assert math.isfinite(elapsed) and elapsed>0
                    times[name].append(elapsed); indices[name].append(int(sample))
                assert all(values==[0,1,2] for values in indices.values()); row['times']=times
            else:
                events=[json.loads(line) for line in stdout.read_text(encoding='utf-8-sig').splitlines() if line.strip()]
                row['events']=events; assert len(events)==2 and events[0]['status']=='body_start'
                event=events[-1]
                assert event['status']=='body_complete' and event['success'] and event['hashes_unchanged'] and event['terminal']
                assert event['runtime']==('cpython' if role=='cpython' else 'xlang3') and event['version_info']==[3,14,7] and Path(event['executable']).resolve()==exe.resolve()
                assert event['optimization_level']==0 and not event['profile_enabled'] and event['repeat']==1
                assert event['benchmark_source_sha256']==PPRINT_BENCHMARK_SHA and event['pprint_source_sha256']==PPRINT_SHA
                assert event['input_length']==100000 and event['input_alias_preserved'] and all(event['prechecks'].values()) and event['result_signature']==SIGNATURE
                assert event['tracked_source_sha256_before']==event['tracked_source_sha256_after']
                elapsed=event['elapsed_seconds_diagnostic_only']; assert math.isfinite(elapsed) and elapsed>0
                row['elapsed_seconds_diagnostic_only']=elapsed
            row['passed']=True
        except BaseException as error: row['error']=type(error).__name__+': '+str(error)
        finally:
            if child is not None and child.poll() is None:
                subprocess.run(['taskkill','/F','/T','/PID',str(child.pid)],capture_output=True,timeout=15)
                if child.poll() is None: child.kill()
                child.wait(timeout=15)
            row['measurement_valid']=finish(); row['passed']=row['passed'] and row['measurement_valid']
            for stream,path in [('stdout',stdout),('stderr',stderr)]: row[stream+'_sha256']=SHA(path) if path.is_file() else None
            save()
        idle('after-'+label); assert stable() and row['passed'],'Retain failed/invalid child; no retry or replacement'
        return row
    def summarize(ratios):
        rng=random.Random(20261008)
        draws=sorted(statistics.median([ratios[rng.randrange(7)] for _ in range(7)]) for _ in range(50000))
        def percentile(q):
            i=q*(len(draws)-1); lo=math.floor(i); hi=math.ceil(i)
            return draws[lo]+(draws[hi]-draws[lo])*(i-lo)
        return dict(pair_ratios=ratios,median_pair_ratio=statistics.median(ratios),bootstrap95_ci=[percentile(.025),percentile(.975)])
    try:
        assert stable()
        if args.mode=='pprint-pairs': record['cpython_parity']=run('cpython',CP,'cpython-parity')
        for index,order in enumerate(ORDERS):
            pair=dict(pair_index=index,order=list(order),runs={}); record['pair_results'].append(pair)
            for role in order: pair['runs'][role]=run(role,(CONTROL if role=='control' else RELEASE)/'xlang3.exe',f'pair-{index+1:02d}-{role}')
            pair['ratios']={case:statistics.median(pair['runs']['control']['times'][case])/statistics.median(pair['runs']['candidate']['times'][case]) for case in CASES} if args.mode=='callback-pairs' else {'pprint_original_body':pair['runs']['control']['elapsed_seconds_diagnostic_only']/pair['runs']['candidate']['elapsed_seconds_diagnostic_only']}
            save(); print('layout pair',index+1,pair['ratios'],flush=True)
        summaries={case:summarize([p['ratios'][case] for p in record['pair_results']]) for case in (CASES if args.mode=='callback-pairs' else ('pprint_original_body',))}
        for case, item in summaries.items():
            item['variation']={}
            for role in ('control','candidate'):
                values=[value for pair in record['pair_results'] for value in pair['runs'][role]['times'][case]] if args.mode=='callback-pairs' else [pair['runs'][role]['elapsed_seconds_diagnostic_only'] for pair in record['pair_results']]
                mean=statistics.mean(values); deviation=statistics.stdev(values)
                item['variation'][role]=dict(all_values=values,count=len(values),mean=mean,stdev=deviation,cv_percent=100*deviation/mean,minimum=min(values),maximum=max(values))
            item['variation_warnings']=[role+' CV exceeds 10%; retain all values' for role, variation in item['variation'].items() if variation['cv_percent']>10]
        selected='sort_callback' if args.mode=='callback-pairs' else 'pprint_original_body'
        summary=dict(summaries[selected],all_cases=summaries)
        summary['useful_signal']=summary['median_pair_ratio']>1.02 and summary['bootstrap95_ci'][0]>1.0
        if args.mode=='callback-pairs': summary['negative_controls_pass']=all(summaries[case]['median_pair_ratio']>=1/1.05 for case in ('loop','small_calls','branch_calls','sort_plain'))
        summary['decision']='signal_for_further_validation_only' if summary['useful_signal'] and summary.get('negative_controls_pass',True) else 'no_useful_affected_signal_or_control_regression'
        record.update(status='terminal_unscored_layout_pairs',summary=summary)
    except BaseException as error: record.update(status='terminal_failed_layout_pairs',error=type(error).__name__+': '+str(error))
    finally:
        record['hashes_after']={p:SHA(p) if Path(p).is_file() else None for p in pins}
        record['hashes_unchanged']=record['hashes_after']==pins and stable()
        if not record['hashes_unchanged']: record['status']='terminal_invalid_hash_drift'
        record.update(terminal=True,completed_utc=datetime.now(timezone.utc).isoformat()); save()
    return 0 if record['status']=='terminal_unscored_layout_pairs' else 1

if __name__=='__main__': raise SystemExit(main())
