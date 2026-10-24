"""Fixed default11 gate and fresh official pickle on XLang3/CPython3.14.7.

The unchanged candidate already passed fresh complete correctness. No reruns,
workload edits, gate weakening or automatic commit; retain every actual phase.
"""
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import statistics
import subprocess
import sys
from datetime import datetime, timezone

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT/'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
RELEASE = ROOT/'build-repro/main-verify-20261006/Release'
BASELINE = ROOT/'build-repro/Release'
PARENT = ROOT/'build-repro/controls/dict-scalar-append-runtime-index-parent-20261008'
PREFIX = 'dict-scalar-append-runtime-index-r3-validation-20261008'
APP = DATA/'dict-scalar-append-runtime-index-r3-applied-source-20261008.json'
FOCUS = DATA/'dict-scalar-append-runtime-index-r3-focused-20261008.json'
CORRECT = DATA/'dict-scalar-append-runtime-index-r3-correctness-20261008.json'
PAIRS = DATA/'dict-scalar-append-original-body-paired-20261008.json'
WATCH = ROOT/'scratch/performance/validate-call-ex-cross-activation-constructor-resume-r3-20261008.py'
RUNNER = ROOT/'benchmarks/diagnostics/run_pyperformance_xlang3_shimmed.py'
SITE = ROOT/'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
read = lambda p: json.loads(Path(p).read_bytes())
tree = lambda root: {p.relative_to(root).as_posix():sha(p) for p in sorted(root.rglob('*')) if p.is_file()}

def main():
    assert sys.version_info[:3] == (3,14,7) and Path(sys.executable).resolve() == CP.resolve() and not sys.flags.optimize
    output = DATA/(PREFIX+'.json'); assert not output.exists()
    pins = {}
    def pin(path, expected=None):
        path = Path(path).resolve(strict=True); value = sha(path)
        assert expected is None or value == expected, str(path)
        pins[str(path)] = value
        return value
    for path,value in ((APP,'dc1b869fe0475e3bdd017aa3b667f755cb7a4dd7ad881494f8afb34af387f45e'),
                       (FOCUS,'0737ff2f405151ed29fdfa22bc0177ea77d18f3b63bdf7aff4662972efb94a38'),
                       (CORRECT,'3d78a8aa54b812289826af708f982a187a348e2272f8e69c0321f965449961f5'),
                       (PAIRS,'c97989d6589be1eb721e608978cc69ae01227b190630ffe4f4e84564fcd1fc0e'),
                       (WATCH,'50007db5cc6adc2e54012fdfbd48514699513f990906d4a508582ba73d6ae0bf'),
                       (CP,'4942b86a6597e5aee0128daa00050ed79bc21f6e709a78eb19cbfeb0c2f39ac9'),
                       (CP.with_name('python314.dll'),'0f9857ffdfe010fe6b99328d58c2e3c7472ce75f336bf9c2ad9bd5bca3bce700')):
        pin(path,value)
    app,focus,correct,pairs = map(read,(APP,FOCUS,CORRECT,PAIRS))
    assert correct['correctness_passed'] and correct['terminal'] and correct['source_inventory_sha256'] == sha(APP)
    assert correct['recorded_sources_match'] and correct['candidate_release_matches_focused'] and correct['fixed_baseline_matches']
    assert correct['fixture_counts'] == dict(core=398,compatibility_sections=11,expected_failure_cases=3)
    assert correct['ctest_count'] == 9 and correct['native_api_checks'] == 2
    for row in correct['phases']:
        assert row['passed'] and row['exit_code'] == 0
        for stream in ('stdout','stderr'): pin(DATA/row[stream+'_log'],row[stream+'_sha256'])
    assert focus['terminal'] and focus['hashes_unchanged'] and focus['status'] == 'targeted_correctness_passed'
    assert focus['source_sha256'] == app['source_sha256'] and len(app['source_sha256']) == 115
    assert pairs['terminal'] and pairs['hashes_unchanged'] and pairs['summary']['useful_signal']
    assert pairs['source_sha256'] == app['source_sha256'] and pairs['binaries_sha256'] == focus['binaries_sha256']
    assert pairs['focused_receipt_sha256'] == sha(FOCUS)
    assert len(pairs['pair_results']) == 7 and len(pairs['raw']) == 14 and all(row['passed'] for row in pairs['raw'])
    for p,value in app['source_sha256'].items(): pin(ROOT/p,value)
    parent_manifest = PARENT/'preserved-release-provenance.json'
    pin(parent_manifest,'35738253af6bd1f41c2a8a6b83233f1007e1ec1d913e234e6d3f3ae971d07707')
    parent = read(parent_manifest)
    release = {Path(p).relative_to(RELEASE.relative_to(ROOT)).as_posix():value for p,value in focus['binaries_sha256'].items()}
    protected = {str(RELEASE):release,str(BASELINE):parent['fixed_baseline_sha256'],
                 str(PARENT/'Release'):parent['files_sha256'],str(PARENT/'source-snapshot'):parent['source_snapshot_sha256']}
    for directory,values in protected.items():
        assert tree(Path(directory)) == values
        for p,value in values.items(): pin(Path(directory)/p,value)
    gate_script = ROOT/'benchmarks/check_regression.py'; pin(gate_script)
    gate_spec = importlib.util.spec_from_file_location('dict_scalar_gate',gate_script)
    gate_module = importlib.util.module_from_spec(gate_spec); gate_spec.loader.exec_module(gate_module)
    assert len(gate_module.CASES) == 11
    gate_sources = {name:pin(ROOT/'benchmarks/cases'/f'{name}.py') for name in gate_module.CASES}
    benchmark = CP.parent/'Lib/site-packages/pyperformance/data-files/benchmarks/bm_pickle'
    pin(benchmark/'run_benchmark.py','31c0e30be79514b45db0d4e858632eb429b0fea3edbac6463ad5f34d92f3cab8')
    pin(benchmark/'bm_pickle_pure_python.toml','846f31a4f830d4b2ab044917d3b3e6b036ace3647445d770c8980f1f5b158c21')
    pin(CP.parent/'Lib/pickle.py','144fdf593814627e13a5fcb0947728e8f0775ecdec181046ee4609063cf232ec')
    pin(ROOT/'benchmarks/diagnostics/pyperf_compat/sitecustomize.py','2ffc217fe20497156bfcb1cebc7fb87203f33c6d4d3dbb6ecbcbcf91ea4cb317')
    pin(RUNNER); pin(ROOT/'benchmarks/diagnostics/preserve_pyperformance_partial.py'); pin(__file__)
    cp_provenance_path = DATA/'pyperformance-cpython3147-live-eval-full-fast-20261007-provenance.json'
    pin(cp_provenance_path,'3d6c8cb2c72c786583c382b835e8e411f56ca285963bc2835f757a13f399f85c')
    cp_provenance = read(cp_provenance_path)
    assert {p.relative_to(SITE).as_posix():pin(p) for p in SITE.glob('*.dist-info/METADATA')} == {p.replace('\\','/'):value for p,value in cp_provenance['dependency_metadata_sha256'].items()}
    spec = importlib.util.spec_from_file_location('dict_scalar_timing_watch',WATCH)
    watcher = importlib.util.module_from_spec(spec); spec.loader.exec_module(watcher)
    def stable(): return all(Path(p).is_file() and sha(p) == value for p,value in pins.items()) and all(tree(Path(p)) == values for p,values in protected.items())
    record = dict(status='running',terminal=False,full_validated=False,whole_goal_complete=False,correctness_reused_same_candidate=True,
                  source_inventory_sha256=sha(APP),source_sha256=app['source_sha256'],source_count=115,
                  correctness_receipt_sha256=sha(CORRECT),paired_receipt_sha256=sha(PAIRS),phases=[],idle_guards=[],
                  hashes_before=dict(pins),fixed_gate=None,official_results={},started_utc=datetime.now(timezone.utc).isoformat())
    def save(): output.write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    def idle(name):
        rows = json.loads(subprocess.check_output(['powershell','-NoProfile','-Command','Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress']).decode('utf-8-sig') or '[]')
        if isinstance(rows,dict): rows = [rows]
        tool_names = {'cl.exe','link.exe','ninja.exe','msbuild.exe','cmake.exe','ctest.exe','nmake.exe','lld-link.exe','clang-cl.exe'}
        busy = [r for r in rows if r['ProcessId'] != os.getpid() and (r['Name'].lower() in tool_names or r['Name'].lower().startswith(('python','xlang3')))]
        record['idle_guards'].append(dict(phase=name,busy=busy)); save(); assert not busy,busy
    environment = dict(os.environ,XLANG3_PYTHON_LIB=str(CP.parent/'Lib'))
    for name in ('PYTHONPATH','PYTHONPYCACHEPREFIX','PYTHONIOENCODING','PYTHONOPTIMIZE'): environment.pop(name,None)
    assert not environment.get('XLANG3_VM_OPCODE_TIMING')
    def phase(name,command,timeout):
        idle('before-'+name); assert stable()
        row = dict(name=name,command=list(map(str,command)),passed=False,timeout=False)
        record['phases'].append(row); save(); finish = watcher.start_timing_process_watch(PREFIX,name,row)
        out,err = DATA/(PREFIX+'-'+name+'.stdout.log'),DATA/(PREFIX+'-'+name+'.stderr.log')
        child = None
        try:
            with out.open('xb') as stdout,err.open('xb') as stderr:
                child = subprocess.Popen(row['command'],cwd=ROOT,env=environment,stdin=subprocess.DEVNULL,stdout=stdout,stderr=stderr,creationflags=subprocess.CREATE_NO_WINDOW)
                row['pid'] = child.pid; save()
                try: row['exit_code'] = child.wait(timeout=timeout)
                except subprocess.TimeoutExpired:
                    row['timeout'] = True
                    subprocess.run(['taskkill','/F','/T','/PID',str(child.pid)],capture_output=True,timeout=15)
                    if child.poll() is None: child.kill()
                    row['exit_code'] = child.wait(timeout=15)
        finally:
            try:
                if child is not None and child.poll() is None:
                    subprocess.run(['taskkill','/F','/T','/PID',str(child.pid)],capture_output=True,timeout=15)
                    if child.poll() is None: child.kill()
                    child.wait(timeout=15)
            finally:
                row.update(stdout_log=out.name,stderr_log=err.name,stdout_sha256=sha(out) if out.exists() else None,stderr_sha256=sha(err) if err.exists() else None)
                row['measurement_valid'] = finish(); save()
        idle('after-'+name); assert stable()
        row['passed'] = row['exit_code'] == 0 and not row['timeout'] and row['measurement_valid']; save()
        assert row['passed'], name
        print(name,'PASS',flush=True)
    def official_summary(path):
        document = read(path)
        rows = [row for row in document['benchmarks'] if row.get('metadata',{}).get('name',document.get('metadata',{}).get('name')) == 'pickle_pure_python']
        assert len(rows) == 1
        metadata = dict(document.get('metadata',{}),**rows[0].get('metadata',{}))
        assert metadata['pickle_module'] == 'pickle' and str(metadata['pickle_protocol']) == '5' and metadata['inner_loops'] == 20
        values = [v for run in rows[0]['runs'] for v in run.get('values',[])]
        assert len(values) == 20 and all(math.isfinite(v) and v > 0 for v in values)
        return dict(output=path.name,sha256=sha(path),values_count=20,mean_seconds=statistics.mean(values),sample_sd_seconds=statistics.stdev(values),metadata=metadata)
    try:
        gate_path = DATA/(PREFIX+'-fixed-gate.json')
        phase('fixed-gate',[CP,gate_script,'--baseline',BASELINE/'xlang3.exe','--candidate',RELEASE/'xlang3.exe','--output',gate_path],900)
        gate = read(gate_path)
        assert gate['status'] == 'pass' and (gate['repeats'],gate['warmup'],gate['threshold']) == (21,5,.1)
        assert set(gate['cases']) == set(gate_sources) and all(row['source_sha256'] == gate_sources[name] for name,row in gate['cases'].items())
        record['fixed_gate'] = dict(output=gate_path.name,sha256=sha(gate_path),exit_code=0); save()
        environment.update(PYTHONPATH=str(ROOT/'benchmarks/diagnostics/pyperf_compat'),PYTHONIOENCODING='utf-8')
        for name,runtime in (('xlang3',RELEASE/'xlang3.exe'),('cpython3147',CP)):
            official = DATA/(PREFIX+'-official-'+name+'-pickle-fast.json')
            phase('official-'+name,[CP,RUNNER,'--runtime',runtime,'--benchmarks','pickle_pure_python','--mode','fast','--case-timeout','300','--dependency-site',SITE,'--output',official],360)
            record['official_results'][name] = official_summary(official); save()
        cp_time = record['official_results']['cpython3147']['mean_seconds']; x_time = record['official_results']['xlang3']['mean_seconds']
        record.update(status='trial_validated',full_validated=True,comparison=dict(speed_cpython_over_xlang3=cp_time/x_time,time_xlang3_over_cpython=x_time/cp_time,scope='Fresh unpaired fast-mode official runs; warnings retained; not a full97 result'))
    except BaseException as error: record.update(status='trial_validation_failed',error=type(error).__name__+': '+str(error))
    finally:
        record.update(terminal=True,hashes_after={p:sha(p) if Path(p).is_file() else None for p in pins},hashes_unchanged=stable(),completed_utc=datetime.now(timezone.utc).isoformat())
        if not record['hashes_unchanged']: record.update(status='invalid_hash_drift',full_validated=False)
        save()
    return 0 if record['status'] == 'trial_validated' else 1

if __name__ == '__main__': raise SystemExit(main())
