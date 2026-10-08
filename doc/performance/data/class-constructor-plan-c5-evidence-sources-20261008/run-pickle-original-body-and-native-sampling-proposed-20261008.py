"""Exact original pickle_pure_python body or separately requested native sampling.

CPython3.14.7 is the manager. Body mode runs CP then fixed X once; sample mode
runs one fixed X child only after same-candidate body parity. Both are unscored.
"""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
EXE = RELEASE / 'xlang3.exe'
CHILD = ROOT / 'scratch/performance/pickle-original-pure-python-diagnostic-proposed-20261008.py'
CHILD_SHA = '5295c899d95d023ca2fb3b511e9bf78b58cb49e4a256770c65adf0d614b14109'
BENCHMARK = CP.parent / 'Lib/site-packages/pyperformance/data-files/benchmarks/bm_pickle/run_benchmark.py'
BENCHMARK_SHA = '31c0e30be79514b45db0d4e858632eb429b0fea3edbac6463ad5f34d92f3cab8'
PICKLE_SHA = '144fdf593814627e13a5fcb0947728e8f0775ecdec181046ee4609063cf232ec'
HOOK = ROOT / 'benchmarks/diagnostics/pyperf_compat/sitecustomize.py'
HOOK_SHA = '2ffc217fe20497156bfcb1cebc7fb87203f33c6d4d3dbb6ecbcbcf91ea4cb317'
PHASES = ['sorted7', 'iteration4', 'nested1', 'canonical10', 'fallback3', 'ownership2', 'owner2', 'namespace4', 'profile3', 'annotation2', 'cpp']
SAMPLER = ROOT / 'scratch/performance/sample-pprint-native-cpu-20261008.exe'
SAMPLER_SHA = '754d9bd7735fbd5f0d3d93f383b4b00ebd052e44eaa74174f2850531072cfa12'
SAMPLER_SOURCE = ROOT / 'scratch/performance/sample-pprint-native-cpu-proposed-20261008.cpp'
SAMPLER_SOURCE_SHA = '00571bcb8c06b72ed98ad085283dcf1a7389f91ebc7bdaa4fba2af33aa65d0a7'
SAMPLER_LOG = ROOT / 'scratch/performance/sample-pprint-native-cpu-build-20261008.log'
SAMPLER_LOG_SHA = 'b17eabbf197e998e6387e3c8f0fcafb758139f2f6f147c85779efc74891d933c'
ANALYZER = ROOT / 'scratch/performance/attribute-sqlglot-native-exports-20261008.py'
ANALYZER_SHA = 'e535c5d629e5fb4bfc1a360f7b730df0a9d13de86c6231c92e39244b1aa7920e'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
read = lambda p: json.loads(Path(p).read_bytes())

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', required=True, choices=('body', 'sample'))
    parser.add_argument('--source-inventory', type=Path, required=True)
    parser.add_argument('--source-inventory-sha256', required=True)
    parser.add_argument('--focused-receipt', type=Path, required=True)
    parser.add_argument('--focused-receipt-sha256', required=True)
    parser.add_argument('--body-receipt', type=Path)
    parser.add_argument('--body-receipt-sha256')
    parser.add_argument('--prefix', required=True)
    args = parser.parse_args()
    assert sys.implementation.name == 'cpython' and sys.version_info[:3] == (3,14,7) and sys.flags.optimize == 0
    assert Path(sys.executable).resolve() == CP.resolve()
    assert re.fullmatch(r'[a-z0-9-]+', args.prefix) and not any(DATA.glob(args.prefix + '*'))
    assert bool(args.body_receipt) == bool(args.body_receipt_sha256)
    assert (args.mode == 'sample') == bool(args.body_receipt)
    output = DATA / (args.prefix + '.json')
    pins, release = {}, {}
    record = dict(status='preflight', terminal=False, mode=args.mode, diagnostic_only=True,
        scored=False, acceptance=False, full_validated=False, fixed_gate=None,
        scope='One original function invocation; no pyperf normalization, speed score, CP win or acceptance claim',
        outer_loops=41, protocol=5, original_dump_operations=2460, profile_enabled=False,
        timeout_seconds=300, idle_guards=[], raw=[], started_utc=datetime.now(timezone.utc).isoformat())
    def save(): output.write_bytes((json.dumps(record, indent=2) + '\n').encode('utf-8'))
    def pin(path, expected=None):
        path = Path(path).resolve(strict=True)
        value = sha(path)
        assert expected is None or value == expected, str(path)
        assert str(path) not in pins or pins[str(path)] == value
        pins[str(path)] = value
        return value
    def release_map(): return {p.relative_to(ROOT).as_posix(): sha(p) for p in sorted(RELEASE.rglob('*')) if p.is_file()}
    def stable(): return all(Path(p).is_file() and sha(p) == value for p,value in pins.items()) and release_map() == release
    def idle(label):
        raw = subprocess.check_output(['powershell', '-NoProfile', '-Command',
            'Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress'])
        rows = json.loads(raw.decode('utf-8-sig') or '[]')
        if isinstance(rows, dict): rows = [rows]
        tools = {'cl.exe','link.exe','ninja.exe','msbuild.exe','cmake.exe','ctest.exe','nmake.exe','lld-link.exe','clang-cl.exe',SAMPLER.name.lower()}
        busy = [r for r in rows if r['ProcessId'] != os.getpid() and
            (r['Name'].lower() in tools or r['Name'].lower().startswith(('python','xlang3')))]
        record['idle_guards'].append(dict(phase=label, allowed_controller_pid=os.getpid(), busy=busy)); save()
        assert not busy, busy
    def validate_child(stdout, stderr, role, executable, row):
        events = [json.loads(line) for line in stdout.read_text(encoding='utf-8-sig').splitlines() if line.strip()]
        row['events'] = events
        assert len(events) == 2 and events[0]['status'] == 'body_start'
        event = events[-1]
        assert event['status'] == 'body_complete' and event['success'] and event['hashes_unchanged']
        assert stderr.read_bytes() == b'' and event['runtime'] == role
        assert event['version_info'] == [3,14,7] and Path(event['executable']).resolve() == executable.resolve()
        assert event['optimization_level'] == 0 and not event['profile_enabled'] and all(event['prechecks'].values())
        assert event['benchmark_source_sha256'] == BENCHMARK_SHA and event['pickle_source_sha256'] == PICKLE_SHA
        assert event['outer_loops'] == 41 and event['protocol'] == 5 and event['repeat'] == 1
        assert event['object_count'] == 3 and event['inner_loops_metadata'] == 20 and event['total_dumps_in_original_body'] == 2460
        assert event['input_and_implementation_identity_preserved'] and event['signature_before'] == event['signature_after']
        assert [r['name'] for r in event['signature_after']] == ['DICT','TUPLE','DICT_GROUP']
        assert all(r['roundtrip_equal'] and r['byte_length'] > 0 for r in event['signature_after'])
        row.update(result_signature=event['signature_after'], elapsed_seconds_diagnostic_only=event['elapsed_seconds_diagnostic_only'],
            original_timer_seconds_diagnostic_only=event['original_timer_seconds_diagnostic_only'])
        return event
    try:
        save(); idle('preflight')
        inventory = read(args.source_inventory); focus = read(args.focused_receipt)
        pin(args.source_inventory, args.source_inventory_sha256); pin(args.focused_receipt, args.focused_receipt_sha256)
        assert focus['terminal'] and focus['hashes_unchanged'] and focus['status'] == 'targeted_correctness_passed'
        assert focus['source_inventory_sha256'] == args.source_inventory_sha256 and focus['source_sha256'] == inventory['source_sha256']
        assert len(focus['phases']) == 11 and {p['name'] for p in focus['phases']} == set(PHASES)
        for row in focus['phases']:
            assert row['exit_code'] == 0 and not row.get('timeout',False) and row.get('passed',True)
            if row['name'] != 'cpp': assert row['output_matches_expected']
            for stream in ('stdout','stderr'): pin(DATA / row[stream + '_log'], row[stream + '_sha256'])
            if row['name'] != 'cpp': assert (DATA / row['stderr_log']).read_bytes() == b''
        release = release_map()
        assert len(release) == 178 and release == focus['binaries_sha256']
        assert sha(EXE) == focus['candidate_binary_sha256']['exe'] and sha(EXE.with_name('xlang3_runtime.dll')) == focus['candidate_binary_sha256']['dll']
        for path,value in {**inventory['source_sha256'], **release}.items(): pin(ROOT / path,value)
        record.update(source_inventory=str(args.source_inventory.resolve()), source_inventory_sha256=args.source_inventory_sha256,
            focused_receipt=str(args.focused_receipt.resolve()), focused_receipt_sha256=args.focused_receipt_sha256,
            source_sha256=inventory['source_sha256'], binaries_sha256=release, release_count=len(release),
            candidate_binary_sha256=focus['candidate_binary_sha256'], focused_phases=PHASES)
        for path,value in ((CHILD,CHILD_SHA),(BENCHMARK,BENCHMARK_SHA),(CP.parent/'Lib/pickle.py',PICKLE_SHA),(HOOK,HOOK_SHA),
            (CP,'4942b86a6597e5aee0128daa00050ed79bc21f6e709a78eb19cbfeb0c2f39ac9'),
            (CP.with_name('python314.dll'),'0f9857ffdfe010fe6b99328d58c2e3c7472ce75f336bf9c2ad9bd5bca3bce700')): pin(path,value)
        for name in ('io.py','datetime.py','random.py','copyreg.py','struct.py','_compat_pickle.py'): pin(CP.parent/'Lib'/name)
        for name in ('_datetime.pyd','_random.pyd','_struct.pyd','_hashlib.pyd'):
            path = CP.parent/'DLLs'/name
            if path.is_file(): pin(path)
        historical_path = DATA/'pyperformance-cpython3147-live-eval-full-fast-20261007-provenance.json'
        pin(historical_path,'3d6c8cb2c72c786583c382b835e8e411f56ca285963bc2835f757a13f399f85c')
        historical = read(historical_path); site = Path(historical['dependency_site']).resolve(strict=True)
        metadata = {p.relative_to(site).as_posix(): pin(p) for p in sorted(site.glob('*.dist-info/METADATA'))}
        assert metadata == {p.replace('\\','/'): v for p,v in historical['dependency_metadata_sha256'].items()}
        for package in (CP.parent/'Lib/site-packages/pyperf',site/'pyperf'):
            for path in sorted(package.rglob('*.py')): pin(path)
        record['controller_sha256'] = pin(__file__); record['child_sha256'] = CHILD_SHA
        environment = os.environ.copy()
        for name in ('PYTHONOPTIMIZE','PYTHONPATH','PYTHONPYCACHEPREFIX','PYTHONIOENCODING','_NT_SYMBOL_PATH','_NT_ALT_SYMBOL_PATH'): environment.pop(name,None)
        assert not environment.get('XLANG3_VM_OPCODE_TIMING')
        environment.update(XLANG3_PYTHON_LIB=str(CP.parent/'Lib'),PYTHONPATH=os.pathsep.join((str(HOOK.parent),str(site))),
            PYTHONIOENCODING='utf-8',PYTHONUNBUFFERED='1',PYTHONPYCACHEPREFIX=str(ROOT/'scratch/performance'/('pycache-'+args.prefix)))
        record['child_environment'] = {name:environment[name] for name in ('XLANG3_PYTHON_LIB','PYTHONPATH','PYTHONIOENCODING','PYTHONUNBUFFERED','PYTHONPYCACHEPREFIX')}
        if args.mode == 'sample':
            pin(args.body_receipt,args.body_receipt_sha256); body = read(args.body_receipt)
            assert body['terminal'] and body['hashes_unchanged'] and body['status'] == 'terminal_unscored_original_body_match' and body['mode'] == 'body'
            assert body['controller_sha256'] == record['controller_sha256'] and body['child_sha256'] == CHILD_SHA
            assert body['source_sha256'] == inventory['source_sha256'] and body['binaries_sha256'] == release
            assert body['source_inventory_sha256'] == args.source_inventory_sha256 and body['focused_receipt_sha256'] == args.focused_receipt_sha256
            assert len(body['raw']) == 2 and all(row['passed'] for row in body['raw'])
            for row in body['raw']:
                for stream in ('stdout','stderr'): pin(DATA/row[stream+'_log'],row[stream+'_sha256'])
            record['body_reference'] = dict(path=str(args.body_receipt.resolve()),sha256=args.body_receipt_sha256)
            record['result_signature'] = body['result_signature']
            pin(SAMPLER,SAMPLER_SHA); pin(SAMPLER_SOURCE,SAMPLER_SOURCE_SHA); pin(SAMPLER_LOG,SAMPLER_LOG_SHA); pin(ANALYZER,ANALYZER_SHA)
            assert not SAMPLER.with_name('dbghelp.dll').exists()
            for name in ('ntdll.dll','ucrtbase.dll','vcruntime140.dll','kernel32.dll','kernelbase.dll','dbghelp.dll'):
                path=Path('C:/Windows/System32')/name
                if path.is_file(): pin(path)
        record['hashes_before'] = dict(pins)
        roles = [('cpython3147',CP),('candidate-xlang3',EXE)] if args.mode == 'body' else [('native-sampling',SAMPLER)]
        for label,executable in roles:
            idle('before-'+label); assert stable()
            stdout=DATA/(args.prefix+'-'+label+'.stdout.log'); stderr=DATA/(args.prefix+'-'+label+'.stderr.log')
            row=dict(runtime=label,stdout_log=stdout.name,stderr_log=stderr.name,timeout=False,passed=False)
            record['raw'].append(row)
            if args.mode == 'body':
                command=[str(executable),str(CHILD),'--benchmark-script',str(BENCHMARK)]
            else:
                childout=DATA/(args.prefix+'-child.stdout.log'); childerr=DATA/(args.prefix+'-child.stderr.log'); samplespath=DATA/(args.prefix+'-samples.jsonl')
                command=[str(SAMPLER),str(EXE),str(CHILD),str(BENCHMARK),str(childout),str(childerr),str(samplespath)]
                row['child_stdout_log']=childout.name; row['child_stderr_log']=childerr.name; row['samples_log']=samplespath.name
            row['command']=command; save(); child=None
            try:
                with stdout.open('xb') as out,stderr.open('xb') as err:
                    child=subprocess.Popen(command,cwd=ROOT,env=environment,stdin=subprocess.DEVNULL,stdout=out,stderr=err,creationflags=subprocess.CREATE_NO_WINDOW)
                    row['pid']=child.pid; save()
                    try: row['exit_code']=child.wait(timeout=300 if args.mode=='body' else 325)
                    except subprocess.TimeoutExpired:
                        row['timeout']=True
                        if args.mode=='body': subprocess.run(['taskkill','/F','/T','/PID',str(child.pid)],capture_output=True,timeout=15)
                        if child.poll() is None: child.kill()
                        row['exit_code']=child.wait(timeout=15)
                assert row['exit_code']==0 and not row['timeout']
                if args.mode=='body':
                    event=validate_child(stdout,stderr,'cpython' if label=='cpython3147' else 'xlang3',executable,row)
                    if label=='cpython3147': record['result_signature']=event['signature_after']
                    assert event['signature_after']==record['result_signature']; row['passed']=True
                else:
                    assert stderr.read_bytes()==b''
                    event=validate_child(childout,childerr,'xlang3',EXE,row)
                    assert event['signature_after']==record['result_signature']
                    rows=[json.loads(line) for line in samplespath.read_text(encoding='utf-8').splitlines() if line.strip()]
                    terminal=[r for r in rows if r['event']=='terminal']
                    assert len(terminal)==1 and rows[-1]==terminal[0]
                    terminal=terminal[0]
                    assert terminal['child_exit_code']==0 and not terminal['timeout'] and terminal['start_seen'] and terminal['end_seen']
                    samples=[r for r in rows if r['event']=='sample']
                    assert samples and len(samples)==terminal['sample_count']
                    record['sampler_terminal']=terminal; record['sample_count']=len(samples)
                    modules={m['base']:m for r in rows if r['event']=='modules' for m in r['items']}
                    spec=importlib.util.spec_from_file_location('pickle_pe_attribution',ANALYZER)
                    analyzer=importlib.util.module_from_spec(spec); spec.loader.exec_module(analyzer); decode=analyzer.demangler()
                    images,labels={},{}
                    def locate(pc):
                        for module in modules.values():
                            if module['base']<=pc<module['base']+module['size']:
                                path=Path(module['path']).resolve(); rva=pc-module['base']; key=(str(path),rva)
                                if key in labels: return labels[key]
                                if str(path) not in pins: result=(path.name+':unproved-image:'+hex(rva),[],'image_not_before_after_pinned')
                                else:
                                    if str(path) not in images:
                                        try: images[str(path)]=analyzer.PE(path)
                                        except Exception: images[str(path)]=None
                                    image=images[str(path)]; function=image.containing(rva) if image else None; anchored=[]; anchor=None
                                    for candidate in image.chain(function) if function else []:
                                        anchored=image.export_names_in(candidate)
                                        if anchored: anchor=candidate; break
                                    if anchored: result=(path.name+':range:'+hex(anchor[0]),sorted({decode(name) for entry,name in anchored}),'export_in_containing_or_chained_pdata')
                                    elif image and rva in image.exports: result=(path.name+':entry:'+hex(rva),sorted({decode(n) for n in image.exports[rva]}),'exact_export_entry')
                                    else: result=(path.name+':unresolved:'+hex(function[0] if function else rva),[],'unresolved_no_exact_range_anchor')
                                labels[key]=result; return result
                        key=('unknown',pc); labels[key]=('unknown-module:'+hex(pc),[],'unknown_module'); return labels[key]
                    exclusive,inclusive=Counter(),Counter()
                    for sample in samples:
                        exclusive[locate(sample['pcs'][0])[0]]+=1
                        groups={locate(pc if index==0 else max(0,pc-1))[0] for index,pc in enumerate(sample['pcs'])}
                        inclusive.update(groups)
                    labelmap={v[0]:v for v in labels.values()}
                    def groups(counter): return [dict(group=k,samples=n,fraction_samples=n/len(samples),names=labelmap[k][1],evidence=labelmap[k][2]) for k,n in counter.most_common()]
                    record['exclusive_leaf_groups']=groups(exclusive); record['inclusive_stack_groups']=groups(inclusive)
                    record['sampling_limits']=['Elapsed time is perturbed and never scored','Leaf locations include inlined work; inclusive stacks overlap and cannot be added',
                        'Polling selects the largest CPU-delta thread, not exact CPU accounting','Start excludes imports; end follows three output audit dumps/loads and reporting',
                        'Only exact current pinned image ranges/export anchors are labelled; unknown PCs remain unresolved','Caller return PCs use PC-1; no Debug PDB or nearest-export attribution']
                    row['passed']=True
            except BaseException as error: row['error']=type(error).__name__+': '+str(error)
            finally:
                if child is not None and child.poll() is None: child.kill(); child.wait(timeout=15)
                for stream,path in (('stdout',stdout),('stderr',stderr)):
                    row[stream+'_sha256']=sha(path) if path.is_file() else None
                if args.mode=='sample':
                    for name,path in (('child_stdout',childout),('child_stderr',childerr),('samples',samplespath)):
                        row[name+'_sha256']=sha(path) if path.is_file() else None
                save()
            assert stable()
        idle('after-children-and-analysis'); assert stable()
        record['status']=('terminal_unscored_original_body_match' if args.mode=='body' else 'terminal_unscored_native_location_diagnostic') if all(r['passed'] for r in record['raw']) else 'terminal_failed_diagnostic_or_output_mismatch'
    except BaseException as error: record.update(status='terminal_failed_diagnostic_controller',error=type(error).__name__+': '+str(error))
    finally:
        record['hashes_after']={p:sha(p) if Path(p).is_file() else None for p in pins}
        record['release_after']=release_map()
        record['hashes_unchanged']=record['hashes_after']==pins and record['release_after']==release
        if not record['hashes_unchanged']: record['status']='terminal_invalid_hash_drift'
        record['terminal']=True; record['completed_utc']=datetime.now(timezone.utc).isoformat(); save()
    print('Original pickle diagnostic terminal:',record['status'],flush=True)
    return 0 if record['status'] in ('terminal_unscored_original_body_match','terminal_unscored_native_location_diagnostic') else 1

if __name__=='__main__': raise SystemExit(main())
