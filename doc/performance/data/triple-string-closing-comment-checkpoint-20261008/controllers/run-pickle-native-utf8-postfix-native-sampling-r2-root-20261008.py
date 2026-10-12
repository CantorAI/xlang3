"""One unscored native location capture of unchanged original pure-Python pickle.

Root execution only, for the validated current native UTF-8 checkpoint. Existing seven
same-candidate body pairs supply byte/roundtrip parity; no CP/body rerun occurs.
The sampler owns exactly one child at the fixed Release path. No cProfile.
"""
import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import math
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
BASELINE = ROOT / 'build-repro/Release'
PARENT = ROOT / 'build-repro/controls/native-str-utf8-encode-parent-20261008'
PARENT_SHA = 'cd5035f531a513104f2948286e81d4996bb01a9b6dc4c84353303f370de4bd97'
CAPTURE_HEAD = '87f5293782d21592df3bcc4ea924e659c906b6da'
INVENTORY = DATA / 'native-str-utf8-applied-source-20261008.json'
INVENTORY_SHA = '96ba3820206e00029a26231b15accb79ff94e1ba33098d769e586d7a28b12f83'
FOCUS = DATA / 'native-str-utf8-focused-20261008.json'
FOCUS_SHA = '92803ffcd0287c7f17bf8431c399519891289ee143304bed7b6b4c6e1c532808'
VALIDATION = DATA / 'native-str-utf8-validation-resume-r2-20261008.json'
VALIDATION_SHA = 'd40024a60374dc2ccfa70a411f5f26f0a4177d4f870f8eb2e70ff0216bf6cba2'
VALIDATION_CONTROLLER = ROOT / 'scratch/performance/validate-native-str-utf8-original-pickle-trial-resume-r2-proposed-20261008.py'
VALIDATION_CONTROLLER_SHA = '3a9b3f7d7289afbf7421d8a910862fd5821915d830a3649e28e11ba62253f3f5'
BUILD = DATA / 'native-str-utf8-build-terminal-20261008.json'
BUILD_SHA = '55fd6ac5c90df146a8c0d6cd280adb2d8c364a93cf6cbb6fa8057db186746055'
PAIRED = DATA / 'native-str-utf8-original-pickle-paired-20261008.json'
PAIRED_SHA = '462c2bc896f601ac94102517ecd8a3646c6810d7320963aa8f152b1b05d2d074'
PAIRED_CONTROLLER = ROOT / 'scratch/performance/measure-native-str-utf8-original-pickle-paired-proposed-20261008.py'
PAIRED_CONTROLLER_SHA = '731363d7878ff2a1e16ff1068d1a3138e928b3d757c46a07b38c18b8ce63f298'
PARENT_BODY = DATA / 'dict-scalar-append-original-body-paired-20261008.json'
PARENT_BODY_SHA = 'c97989d6589be1eb721e608978cc69ae01227b190630ffe4f4e84564fcd1fc0e'
PARENT_BODY_CONTROLLER = ROOT / 'scratch/performance/measure-dict-scalar-append-original-body-paired-r2-proposed-20261008.py'
PARENT_BODY_CONTROLLER_SHA = 'de0aaf91f5c31f3e14d6a18d780440670c44dfbf157bd3c608d545f591852f0f'
WATCH = ROOT / 'scratch/performance/validate-call-ex-cross-activation-constructor-resume-r3-20261008.py'
WATCH_SHA = '50007db5cc6adc2e54012fdfbd48514699513f990906d4a508582ba73d6ae0bf'
CP_BODY = DATA / 'pickle-frame-locals-retirement-r4-original-body-r2-20261008.json'
CP_BODY_SHA = 'fab10dd8e053a9ca82448ba8eccc9e1cb339633602b6dcdb408d844aab882576'
CP_BODY_CONTROLLER = ROOT / 'scratch/performance/run-pickle-frame-retirement-r4-original-body-and-sampling-proposed-20261008.py'
CP_BODY_CONTROLLER_SHA = '04c423462a4902a917061319e3488711c60d04caf193581d2be258b44d1f8d13'
INPUTS = ROOT / 'scratch/performance/pickle-native-utf8-postfix-native-sampling-inputs-proposed-20261008.json'
INPUTS_SHA = 'a8be77e20bab1a752a870872e194ecabe3354626398916d6e155510002b282d5'
CHILD = ROOT / 'scratch/performance/pickle-original-pure-python-diagnostic-proposed-20261008.py'
CHILD_SHA = '5295c899d95d023ca2fb3b511e9bf78b58cb49e4a256770c65adf0d614b14109'
BENCHMARK = CP.parent / 'Lib/site-packages/pyperformance/data-files/benchmarks/bm_pickle/run_benchmark.py'
BENCHMARK_SHA = '31c0e30be79514b45db0d4e858632eb429b0fea3edbac6463ad5f34d92f3cab8'
PICKLE_SHA = '144fdf593814627e13a5fcb0947728e8f0775ecdec181046ee4609063cf232ec'
HOOK = ROOT / 'benchmarks/diagnostics/pyperf_compat/sitecustomize.py'
HOOK_SHA = '2ffc217fe20497156bfcb1cebc7fb87203f33c6d4d3dbb6ecbcbcf91ea4cb317'
SAMPLER = ROOT / 'scratch/performance/sample-pprint-native-cpu-20261008.exe'
SAMPLER_SHA = '754d9bd7735fbd5f0d3d93f383b4b00ebd052e44eaa74174f2850531072cfa12'
SAMPLER_SOURCE = ROOT / 'scratch/performance/sample-pprint-native-cpu-proposed-20261008.cpp'
SAMPLER_SOURCE_SHA = '00571bcb8c06b72ed98ad085283dcf1a7389f91ebc7bdaa4fba2af33aa65d0a7'
SAMPLER_LOG = ROOT / 'scratch/performance/sample-pprint-native-cpu-build-20261008.log'
SAMPLER_LOG_SHA = 'b17eabbf197e998e6387e3c8f0fcafb758139f2f6f147c85779efc74891d933c'
ANALYZER = ROOT / 'scratch/performance/attribute-sqlglot-native-exports-20261008.py'
ANALYZER_SHA = 'e535c5d629e5fb4bfc1a360f7b730df0a9d13de86c6231c92e39244b1aa7920e'
PHASES = ['cpp','native_str_utf8_encode','string_compat','unicode_repr_surrogate','pickle_module',
    'sys_monitoring_all_events','trace_hooks','nested_profile_setting']
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
read = lambda p: json.loads(Path(p).read_bytes())
tree = lambda directory: {p.relative_to(directory).as_posix(): sha(p) for p in sorted(directory.rglob('*')) if p.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prefix', required=True)
    args = parser.parse_args()
    if sys.implementation.name != 'cpython' or sys.version_info[:3] != (3,14,7) or sys.flags.optimize:
        raise RuntimeError('Only unoptimized CPython 3.14.7 may manage this capture')
    if Path(sys.executable).resolve() != CP.resolve():
        raise RuntimeError('Use the fixed CPython 3.14.7 executable')
    assert args.prefix == 'pickle-native-utf8-postfix-native-sampling-r2-20261008' and not any(DATA.glob(args.prefix + '*'))
    assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip() == CAPTURE_HEAD
    assert not subprocess.check_output(['git','diff','--cached','--name-only'],cwd=ROOT)
    output = DATA / (args.prefix + '.json')
    pins, release, protected, objects, dirty = {}, {}, {}, {}, {}
    identity_ready = False
    record = dict(status='preflight', terminal=False, mode='sample', diagnostic_only=True,
        scored=False, acceptance=False, profile_enabled=False, fresh_body_rerun=False,
        scope='One fixed XLang3 child under the existing native sampler; location evidence only',
        outer_loops=41, protocol=5, original_dump_operations=2460, timeout_seconds=300,
        parent_timeout_seconds=325, idle_guards=[], raw=[], started_utc=datetime.now(timezone.utc).isoformat())

    def save(): output.write_bytes((json.dumps(record, indent=2) + '\n').encode('utf-8'))
    def pin(path, expected=None):
        path = Path(path).resolve(strict=True)
        value = sha(path)
        assert expected is None or value == expected, str(path)
        assert str(path) not in pins or pins[str(path)] == value
        pins[str(path)] = value
        return value
    def release_map(): return {p.relative_to(ROOT).as_posix(): sha(p) for p in sorted(RELEASE.rglob('*')) if p.is_file()}
    def object_map():
        return {p.relative_to(ROOT).as_posix(): sha(p) for root in objects['object_roots']
            for p in sorted((ROOT/root).rglob('*.obj')) if p.is_file()}
    def tracked_dirty():
        paths = subprocess.check_output(['git','diff','HEAD','--name-only','-z'], cwd=ROOT).decode('utf-8').split('\0')
        return {p: sha(ROOT/p) if (ROOT/p).is_file() else None for p in paths if p}
    def stable():
        return (all(Path(p).is_file() and sha(p) == h for p,h in pins.items())
            and release_map() == release and all(tree(Path(p)) == values for p,values in protected.items())
            and object_map() == record['object_sha256'] and tracked_dirty() == dirty
            and subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip() == CAPTURE_HEAD)
    def idle(label):
        raw = subprocess.check_output(['powershell','-NoProfile','-Command',
            'Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress'])
        rows = json.loads(raw.decode('utf-8-sig') or '[]')
        if isinstance(rows,dict): rows = [rows]
        tools = {'cl.exe','link.exe','ninja.exe','msbuild.exe','cmake.exe','ctest.exe','nmake.exe','lld-link.exe','clang-cl.exe',SAMPLER.name.lower()}
        busy = [r for r in rows if r['ProcessId'] != os.getpid() and
            (r['Name'].lower() in tools or r['Name'].lower().startswith(('python','xlang3')))]
        record['idle_guards'].append(dict(phase=label,allowed_controller_pid=os.getpid(),busy=busy)); save()
        assert not busy, busy
    def validate_child(stdout,stderr,role,executable,row):
        events = [json.loads(line) for line in stdout.read_text(encoding='utf-8-sig').splitlines() if line.strip()]
        row['events'] = events
        assert len(events) == 2 and events[0]['status'] == 'body_start'
        event = events[-1]
        assert event['status'] == 'body_complete' and event['success'] and event['hashes_unchanged']
        assert stderr.read_bytes() == b'' and event['runtime'] == role
        for item in events:
            assert item['version_info'] == [3,14,7] and Path(item['executable']).resolve() == executable.resolve()
            assert item['optimization_level'] == 0 and not item['profile_enabled'] and all(item['prechecks'].values())
            assert item['benchmark_source_sha256'] == BENCHMARK_SHA and item['pickle_source_sha256'] == PICKLE_SHA
            assert item['outer_loops'] == 41 and item['protocol'] == 5 and item['repeat'] == 1
            assert item['object_count'] == 3 and item['inner_loops_metadata'] == 20 and item['total_dumps_in_original_body'] == 2460
            assert item['signature_before'] == record['result_signature']
        assert event['input_and_implementation_identity_preserved'] and event['signature_before'] == event['signature_after']
        assert event['signature_after'] == record['result_signature']
        assert [r['name'] for r in event['signature_after']] == ['DICT','TUPLE','DICT_GROUP']
        assert all(r['roundtrip_equal'] and r['byte_length'] > 0 for r in event['signature_after'])
        row.update(result_signature=event['signature_after'],elapsed_seconds_diagnostic_only=event['elapsed_seconds_diagnostic_only'],
            original_timer_seconds_diagnostic_only=event['original_timer_seconds_diagnostic_only'])
        return event
    def pin_streams(row):
        for stream in ('stdout','stderr'): pin(DATA/row[stream+'_log'],row[stream+'_sha256'])
    def pin_watch(row):
        watch = row['external_process_watch']
        assert row['measurement_valid'] and watch['measurement_valid'] and not watch['overlaps'] and not watch['scanner_errors']
        pin(DATA/watch['log'],watch['sha256'])

    try:
        save(); idle('preflight')
        failed_path = DATA / 'pickle-native-utf8-postfix-native-sampling-20261008.json'
        pin(failed_path, 'e8b357b75498aec149f1eec0787c9a63926c12fba2b27d0b08f1c4555a527ffe')
        failed = read(failed_path)
        assert failed['terminal'] and failed['status'] == 'terminal_failed_diagnostic_controller'
        assert failed['raw'] == [] and not failed['identity_preflight_complete']
        record['preserved_failed_preflight_sha256'] = pins[str(failed_path.resolve())]
        for path,h in ((INVENTORY,INVENTORY_SHA),(FOCUS,FOCUS_SHA),(VALIDATION,VALIDATION_SHA),
            (VALIDATION_CONTROLLER,VALIDATION_CONTROLLER_SHA),(BUILD,BUILD_SHA),(PAIRED,PAIRED_SHA),
            (PAIRED_CONTROLLER,PAIRED_CONTROLLER_SHA),(PARENT_BODY,PARENT_BODY_SHA),
            (PARENT_BODY_CONTROLLER,PARENT_BODY_CONTROLLER_SHA),(CP_BODY,CP_BODY_SHA),
            (CP_BODY_CONTROLLER,CP_BODY_CONTROLLER_SHA),(INPUTS,INPUTS_SHA),
            (PARENT/'preserved-release-provenance.json',PARENT_SHA),(WATCH,WATCH_SHA)): pin(path,h)
        inventory,focus,trial,build,paired,parent_body,cp_body,objects,parent = map(read,
            (INVENTORY,FOCUS,VALIDATION,BUILD,PAIRED,PARENT_BODY,CP_BODY,INPUTS,PARENT/'preserved-release-provenance.json'))
        sources = inventory['source_sha256']
        assert len(sources) == inventory['source_count'] == 119
        assert focus['terminal'] and focus['hashes_unchanged'] and focus['status'] == 'targeted_correctness_passed'
        assert focus['source_inventory_sha256'] == INVENTORY_SHA and focus['source_sha256'] == sources
        assert [r['name'] for r in focus['phases']] == PHASES
        for row in focus['phases']:
            assert row['passed'] and row['exit_code'] == 0 and not row.get('timeout',False); pin_streams(row)
            assert (DATA/row['stderr_log']).read_bytes() == b''
            if row['name'] != 'cpp':
                pin(ROOT/row['source'],row['source_sha256']); pin(ROOT/row['expected'],row['expected_sha256'])
                assert row['command'] == [str(EXE),str(ROOT/row['source'])]
                assert (DATA/row['stdout_log']).read_bytes().replace(b'\r\n',b'\n') == (ROOT/row['expected']).read_bytes().replace(b'\r\n',b'\n')
        assert trial['terminal'] and trial['full_validated'] and trial['hashes_unchanged'] and trial['status'] == 'trial_validated'
        assert trial['source_sha256'] == sources and trial['source_inventory_sha256'] == INVENTORY_SHA
        assert trial['paired_receipt_sha256'] == PAIRED_SHA and trial['focused_receipt_sha256'] == FOCUS_SHA
        assert trial['build_receipt_sha256'] == BUILD_SHA and trial['correctness_passed'] and not trial['whole_goal_complete']
        assert trial['hashes_before'] == trial['hashes_after'] and trial['fixed_gate']['exit_code'] == 0
        assert trial['fixture_counts'] == dict(core=399,compatibility_sections=11,expected_failure_cases=3)
        assert trial['ctest_count'] == 9 and trial['native_api_checks'] == 2
        assert trial['reused_phase_names'] == ['full-fixtures-clean','ctest-inventory','ctest']
        assert all(trial['correctness_phase_semantics'].values())
        classification = trial['untimed_ctest_classification']
        assert classification['semantic_passed'] and classification['owned_pid'] == 35388
        assert not classification['original_passed'] and not classification['original_measurement_valid'] and not classification['timed_phase_watch_exception']
        for path,h in trial['hashes_after'].items(): pin(path,h)
        for row in trial['phases']:
            assert row['exit_code'] == 0 and not row['timeout'] and row['owned_child_cleanup_completed']; pin_streams(row)
            watch = row['external_process_watch']; pin(DATA/watch['log'],watch['sha256'])
            if row.get('execution') == 'historical_same_candidate_untimed_correctness':
                assert row['semantic_passed'] and not row['executed_again'] and not row['timing_accepted']
                if row['name'] == 'ctest':
                    assert not row['passed'] and not row['measurement_valid'] and not watch['measurement_valid']
                    assert row['owner_observer_false_positive'] and row['pid'] == 35388
                else: pin_watch(row)
            else: assert row['passed']; pin_watch(row)
        gate = read(DATA/trial['fixed_gate']['output'])
        pin(DATA/trial['fixed_gate']['output'],trial['fixed_gate']['sha256'])
        assert gate['status'] == 'pass' and len(gate['cases']) == 11 and (gate['repeats'],gate['warmup'],gate['threshold']) == (21,5,.1)
        for result in trial['official_results'].values():
            assert result['values_count'] == 20 and result['metadata']['name'] == 'pickle_pure_python'
            assert result['metadata']['pickle_protocol'] == '5' and result['metadata']['inner_loops'] == 20
            pin(DATA/result['output'],result['sha256'])
        assert build['terminal'] and build['status'] == 'build_passed' and build['exit_code'] == build['actual_root_tool_exit_code'] == 0
        assert build['applied_source_sha256'] == INVENTORY_SHA; pin(ROOT/build['log'],build['log_sha256'])
        record['official_reference'] = dict(validation_sha256=VALIDATION_SHA,
            output_checks='Original protocol5/inner20/20-value metadata; byte parity is authenticated from current UTF8 pairs',results=trial['official_results'])
        release = release_map()
        assert len(release) == 178 and release == focus['binaries_sha256'] == trial['binaries_sha256']
        assert sha(EXE) == focus['candidate_binary_sha256']['exe'] and sha(EXE.with_name('xlang3_runtime.dll')) == focus['candidate_binary_sha256']['dll']
        for path,h in {**sources,**release}.items(): pin(ROOT/path,h)
        assert parent['terminal'] and parent['full_validated'] and parent['source_count'] == 117 and parent['file_count'] == 178 and parent['object_count'] == 149
        for directory,values in ((PARENT/'Release',parent['files_sha256']),
            (PARENT/'source-snapshot',parent['source_snapshot_sha256']),(BASELINE,parent['fixed_baseline_sha256'])):
            for path,h in values.items(): pin(directory/path,h)
        protected[str(PARENT)] = dict({'Release/'+p:h for p,h in parent['files_sha256'].items()},
            **{'source-snapshot/'+p:h for p,h in parent['source_snapshot_sha256'].items()},
            **{'native-objects/'+p:h for p,h in parent['object_snapshot_sha256'].items()},
            **{'build-metadata/'+p:h for p,h in parent['build_metadata_sha256'].items()},
            **{'preserved-release-provenance.json':PARENT_SHA})
        assert tree(PARENT) == protected[str(PARENT)]
        assert len(parent['fixed_baseline_sha256']) == 177 and parent['fixed_baseline_sha256'] == trial['baseline_sha256']
        protected[str(BASELINE)] = parent['fixed_baseline_sha256']; assert tree(BASELINE) == protected[str(BASELINE)]
        record.update(source_inventory=str(INVENTORY),source_inventory_sha256=INVENTORY_SHA,source_sha256=sources,
            recorded_source_count=119,focused_receipt_sha256=FOCUS_SHA,validation_sha256=VALIDATION_SHA,
            binaries_sha256=release,release_count=178,candidate_binary_sha256=focus['candidate_binary_sha256'],
            parent_manifest_sha256=PARENT_SHA,baseline_sha256=parent['fixed_baseline_sha256'],
            historical_ctest_semantic_classification=classification,full97_current_binary_proof_used=False)
        assert paired['terminal'] and paired['hashes_unchanged'] and paired['status'] == 'terminal_unscored_paired_original_body_diagnostic'
        assert paired['controller_sha256'] == PAIRED_CONTROLLER_SHA and paired['child_sha256'] == CHILD_SHA
        assert paired['source_sha256'] == sources and paired['binaries_sha256'] == release
        assert paired['source_inventory_sha256'] == INVENTORY_SHA and paired['focused_receipt_sha256'] == FOCUS_SHA
        assert paired['hashes_before'] == paired['hashes_after'] and paired['frozen_original_parity_receipt_sha256'] == PARENT_BODY_SHA
        assert paired['parent_manifest_sha256'] == PARENT_SHA and paired['pair_count'] == 7
        assert len(paired['raw']) == 14 and len(paired['pair_results']) == 7
        record['result_signature'] = paired['result_signature']
        for row in paired['raw']:
            assert row['passed'] and row['exit_code'] == 0 and not row['timeout'] and row['owned_child_cleanup_completed']
            pin_streams(row); pin_watch(row)
            executable = EXE if row['runtime'] == 'candidate' else PARENT/'Release/xlang3.exe'
            assert row['runtime'] in ('parent','candidate')
            assert row['command'] == [str(executable),str(CHILD),'--benchmark-script',str(BENCHMARK)]
            validate_child(DATA/row['stdout_log'],DATA/row['stderr_log'],'xlang3',executable,{})
        assert len([r for r in paired['raw'] if r['runtime'] == 'candidate']) == 7
        assert parent_body['terminal'] and parent_body['hashes_unchanged'] and parent_body['controller_sha256'] == PARENT_BODY_CONTROLLER_SHA
        assert parent_body['child_sha256'] == CHILD_SHA and parent_body['frozen_original_parity_receipt_sha256'] == CP_BODY_SHA
        assert parent_body['result_signature'] == record['result_signature']
        assert parent_body['binaries_sha256'] == {RELEASE.relative_to(ROOT).as_posix()+'/'+p:h for p,h in parent['files_sha256'].items()}
        assert cp_body['terminal'] and cp_body['hashes_unchanged'] and cp_body['status'] == 'terminal_unscored_original_body_match'
        assert cp_body['controller_sha256'] == CP_BODY_CONTROLLER_SHA and cp_body['child_sha256'] == CHILD_SHA
        assert cp_body['hashes_before'] == cp_body['hashes_after'] and cp_body['result_signature'] == record['result_signature']
        assert [r['runtime'] for r in cp_body['raw']] == ['cpython3147','candidate-xlang3']
        for row in cp_body['raw']:
            assert row['passed'] and row['exit_code'] == 0 and not row['timeout']; pin_streams(row)
        cprow = cp_body['raw'][0]
        assert cprow['command'] == [str(CP),str(CHILD),'--benchmark-script',str(BENCHMARK)]
        validate_child(DATA/cprow['stdout_log'],DATA/cprow['stderr_log'],'cpython',CP,{})
        record['body_reference'] = dict(kind='reused_same_candidate_seven_pair_original_body_parity',
            path=str(PAIRED),sha256=PAIRED_SHA,producer_sha256=PAIRED_CONTROLLER_SHA,candidate_rows=7,
            historical_cpython_body_path=str(CP_BODY),historical_cpython_body_sha256=CP_BODY_SHA,
            historical_parent_body_path=str(PARENT_BODY),historical_parent_body_sha256=PARENT_BODY_SHA,
            produced_by_this_sampling_controller=False,fresh_cpython_body_rerun=False)
        for path,h in ((CHILD,CHILD_SHA),(BENCHMARK,BENCHMARK_SHA),(CP.parent/'Lib/pickle.py',PICKLE_SHA),(HOOK,HOOK_SHA),
            (CP,'4942b86a6597e5aee0128daa00050ed79bc21f6e709a78eb19cbfeb0c2f39ac9'),
            (CP.with_name('python314.dll'),'0f9857ffdfe010fe6b99328d58c2e3c7472ce75f336bf9c2ad9bd5bca3bce700'),
            (SAMPLER,SAMPLER_SHA),(SAMPLER_SOURCE,SAMPLER_SOURCE_SHA),(SAMPLER_LOG,SAMPLER_LOG_SHA),(ANALYZER,ANALYZER_SHA)): pin(path,h)
        assert not SAMPLER.with_name('dbghelp.dll').exists()
        for name in ('io.py','datetime.py','random.py','copyreg.py','struct.py','_compat_pickle.py'): pin(CP.parent/'Lib'/name)
        for name in ('_datetime.pyd','_random.pyd','_struct.pyd','_hashlib.pyd'):
            path = CP.parent/'DLLs'/name
            if path.is_file(): pin(path)
        for name in ('ntdll.dll','ucrtbase.dll','vcruntime140.dll','kernel32.dll','kernelbase.dll','dbghelp.dll'):
            path = Path('C:/Windows/System32')/name
            if path.is_file(): pin(path)
        historical_path = DATA/'pyperformance-cpython3147-live-eval-full-fast-20261007-provenance.json'
        pin(historical_path,'3d6c8cb2c72c786583c382b835e8e411f56ca285963bc2835f757a13f399f85c')
        historical = read(historical_path); site = Path(historical['dependency_site']).resolve(strict=True)
        metadata = {p.relative_to(site).as_posix(): pin(p) for p in sorted(site.glob('*.dist-info/METADATA'))}
        assert metadata == {p.replace('\\','/'): h for p,h in historical['dependency_metadata_sha256'].items()}
        assert site == Path('D:/CantorAI/xlang3/venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages').resolve()
        for package in (CP.parent/'Lib/site-packages/pyperf',site/'pyperf'):
            for path in sorted(package.rglob('*.py')): pin(path)
        assert objects['source_inventory_sha256'] == INVENTORY_SHA and objects['object_count'] == 149
        assert objects['candidate_binary_sha256'] == focus['candidate_binary_sha256']
        record['object_sha256'] = {p:r['object_sha256'] for p,r in objects['objects'].items()}
        for path,row in objects['objects'].items():
            pin(ROOT/path,row['object_sha256']); pin(ROOT/row['source'],row['source_sha256'])
            assert row['source_in_recorded_inventory'] == (row['source'] in sources)
            if row['source'] in sources: assert sources[row['source']] == row['source_sha256']
        assert object_map() == record['object_sha256']
        for path,h in objects['build_metadata_sha256'].items(): pin(ROOT/path,h)
        record['native_object_inputs'] = objects
        record['native_object_inputs_sha256'] = INPUTS_SHA
        dirty = tracked_dirty()
        record['tracked_dirty_sha256_before'] = dirty
        record['git_head_at_capture'] = subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
        assert record['git_head_at_capture'] == CAPTURE_HEAD
        record['accepted_checkpoint_head'] = CAPTURE_HEAD
        record['source_identity_limits'] = [objects['source_identity_limit'],
            'Current dirty files are identity guards, not included ownership or clean-build claims',
            'Historical CP metadata coverage does not establish historical dependency source coverage',
            'Fresh attribution must use current objects and sampled module bases; no prior RVAs/counts reused',
            'No old full97 receipt or historical body binary is used as proof of the current executable']
        record['controller_sha256'] = pin(__file__); record['child_sha256'] = CHILD_SHA
        environment = os.environ.copy()
        for name in ('PYTHONOPTIMIZE','PYTHONPATH','PYTHONPYCACHEPREFIX','PYTHONIOENCODING','_NT_SYMBOL_PATH','_NT_ALT_SYMBOL_PATH'): environment.pop(name,None)
        assert not environment.get('XLANG3_VM_OPCODE_TIMING')
        environment.update(XLANG3_PYTHON_LIB=str(CP.parent/'Lib'),PYTHONPATH=os.pathsep.join((str(HOOK.parent),str(site))),
            PYTHONIOENCODING='utf-8',PYTHONUNBUFFERED='1',PYTHONPYCACHEPREFIX=str(ROOT/'scratch/performance'/('pycache-'+args.prefix)))
        record['child_environment'] = {name:environment[name] for name in ('XLANG3_PYTHON_LIB','PYTHONPATH','PYTHONIOENCODING','PYTHONUNBUFFERED','PYTHONPYCACHEPREFIX')}
        pin(WATCH,WATCH_SHA)
        watchspec = importlib.util.spec_from_file_location('utf8_native_capture_watch',WATCH)
        watcher = importlib.util.module_from_spec(watchspec); watchspec.loader.exec_module(watcher)
        record['hashes_before'] = dict(pins)
        identity_ready = True
        idle('before-native-sampling'); assert stable()
        stdout = DATA/(args.prefix+'-native-sampling.stdout.log'); stderr = DATA/(args.prefix+'-native-sampling.stderr.log')
        childout = DATA/(args.prefix+'-child.stdout.log'); childerr = DATA/(args.prefix+'-child.stderr.log'); samplespath = DATA/(args.prefix+'-samples.jsonl')
        row = dict(runtime='native-sampling',stdout_log=stdout.name,stderr_log=stderr.name,
            child_stdout_log=childout.name,child_stderr_log=childerr.name,samples_log=samplespath.name,timeout=False,passed=False)
        command = [str(SAMPLER),str(EXE),str(CHILD),str(BENCHMARK),str(childout),str(childerr),str(samplespath)]
        row['command'] = command; record['raw'].append(row); save(); child = None
        finish_watch = watcher.start_timing_process_watch(args.prefix,'native-sampling',row)
        try:
            with stdout.open('xb') as out,stderr.open('xb') as err:
                child = subprocess.Popen(command,cwd=ROOT,env=environment,stdin=subprocess.DEVNULL,
                    stdout=out,stderr=err,creationflags=subprocess.CREATE_NO_WINDOW)
                row['pid'] = child.pid; save()
                try: row['exit_code'] = child.wait(timeout=325)
                except subprocess.TimeoutExpired:
                    row['timeout'] = True
                    if child.poll() is None: child.kill()
                    row['exit_code'] = child.wait(timeout=15)
            assert row['exit_code'] == 0 and not row['timeout'] and stderr.read_bytes() == b''
            validate_child(childout,childerr,'xlang3',EXE,row)
            rows = [json.loads(line) for line in samplespath.read_text(encoding='utf-8').splitlines() if line.strip()]
            terminals = [r for r in rows if r['event'] == 'terminal']
            assert len(terminals) == 1 and rows[-1] == terminals[0]
            terminal = terminals[0]
            assert terminal['child_exit_code'] == 0 and not terminal['timeout'] and terminal['start_seen'] and terminal['end_seen']
            samples = [r for r in rows if r['event'] == 'sample']
            assert samples and len(samples) == terminal['sample_count']
            record['sampler_terminal'] = terminal; record['sample_count'] = len(samples)
            assert all(math.isfinite(terminal[k]) and terminal[k] >= 0 for k in ('total_pause_seconds','maximum_pause_seconds'))
            record['pause_statistics'] = {k:terminal[k] for k in ('total_pause_seconds','maximum_pause_seconds','external_suspensions','capture_errors','discarded_terminal_races')}
            modules = {m['base']:m for r in rows if r['event'] == 'modules' for m in r['items']}
            record['sampled_modules'] = list(modules.values())
            spec = importlib.util.spec_from_file_location('pickle_pe_attribution',ANALYZER)
            analyzer = importlib.util.module_from_spec(spec); spec.loader.exec_module(analyzer); decode = analyzer.demangler()
            images,labels = {},{}
            def locate(pc):
                for module in modules.values():
                    if module['base'] <= pc < module['base']+module['size']:
                        path = Path(module['path']).resolve(); rva = pc-module['base']; key = (str(path),rva)
                        if key in labels: return labels[key]
                        if str(path) not in pins: result = (path.name+':unproved-image:'+hex(rva),[],'image_not_before_after_pinned')
                        else:
                            if str(path) not in images:
                                try: images[str(path)] = analyzer.PE(path)
                                except Exception: images[str(path)] = None
                            image = images[str(path)]; function = image.containing(rva) if image else None; anchored = []; anchor = None
                            for candidate in image.chain(function) if function else []:
                                anchored = image.export_names_in(candidate)
                                if anchored: anchor = candidate; break
                            if anchored: result = (path.name+':range:'+hex(anchor[0]),sorted({decode(name) for entry,name in anchored}),'export_in_containing_or_chained_pdata')
                            elif image and rva in image.exports: result = (path.name+':entry:'+hex(rva),sorted({decode(n) for n in image.exports[rva]}),'exact_export_entry')
                            else: result = (path.name+':unresolved:'+hex(function[0] if function else rva),[],'unresolved_no_exact_range_anchor')
                        labels[key] = result; return result
                key = ('unknown',pc); labels[key] = ('unknown-module:'+hex(pc),[],'unknown_module'); return labels[key]
            exclusive,inclusive = Counter(),Counter()
            for sample in samples:
                exclusive[locate(sample['pcs'][0])[0]] += 1
                inclusive.update({locate(pc if index == 0 else max(0,pc-1))[0] for index,pc in enumerate(sample['pcs'])})
            labelmap = {v[0]:v for v in labels.values()}
            def groups(counter):
                return [dict(group=k,samples=n,fraction_samples=n/len(samples),names=labelmap[k][1],evidence=labelmap[k][2]) for k,n in counter.most_common()]
            record['exclusive_leaf_groups'] = groups(exclusive); record['inclusive_stack_groups'] = groups(inclusive)
            record['sampling_limits'] = ['Elapsed time is perturbed and never scored',
                'Leaf locations include inlined work; inclusive stacks overlap and cannot be added',
                'Polling selects the largest CPU-delta thread, not exact CPU accounting',
                'Start excludes imports; end follows three output audit dumps/loads and reporting',
                'Only exact current pinned image ranges/export anchors are labelled; unknown PCs remain unresolved',
                'Caller return PCs use PC-1; no Debug PDB or nearest-export attribution',
                'Object hashes preserve attribution eligibility, not support for opaque /GL COFF formats']
            row['passed'] = True
        except BaseException as error: row.update(passed=False,error=type(error).__name__+': '+str(error))
        finally:
            try:
                if child is not None and child.poll() is None: child.kill(); child.wait(timeout=15)
                row['owned_sampler_cleanup_completed'] = True
            except BaseException as error: row.update(passed=False,owned_sampler_cleanup_completed=False,cleanup_error=repr(error))
            finally:
                for name,path in (('stdout',stdout),('stderr',stderr),('child_stdout',childout),('child_stderr',childerr),('samples',samplespath)):
                    try: row[name+'_sha256'] = sha(path) if path.is_file() else None
                    except BaseException as error: row.update(passed=False); row[name+'_hash_error'] = repr(error)
                try: row['measurement_valid'] = finish_watch()
                except BaseException as error: row.update(measurement_valid=False,watch_finish_error=repr(error))
                row['passed'] = row['passed'] and row['measurement_valid']
                save()
        idle('after-native-sampling-and-file-attribution'); assert stable()
        record['status'] = 'terminal_unscored_native_location_diagnostic' if row['passed'] else 'terminal_failed_diagnostic_or_output_mismatch'
    except BaseException as error: record.update(status='terminal_failed_diagnostic_controller',error=type(error).__name__+': '+str(error))
    finally:
        try:
            record['hashes_after'] = {p:sha(p) if Path(p).is_file() else None for p in pins}
            record['release_after'] = release_map()
            record['object_sha256_after'] = object_map() if objects else {}
            record['tracked_dirty_sha256_after'] = tracked_dirty()
            record['identity_preflight_complete'] = identity_ready
            record['partial_pinned_hashes_unchanged'] = record['hashes_after'] == pins
            record['hashes_unchanged'] = (identity_ready and record['hashes_after'] == pins and record['release_after'] == release
                and all(tree(Path(p)) == values for p,values in protected.items())
                and record['object_sha256_after'] == record.get('object_sha256',{}) and record['tracked_dirty_sha256_after'] == dirty
                and subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip() == CAPTURE_HEAD)
            if identity_ready and not record['hashes_unchanged']: record['status'] = 'terminal_invalid_hash_drift'
        except BaseException as error: record.update(status='terminal_invalid_final_identity_check',hashes_unchanged=False,final_identity_error=repr(error))
        record['terminal'] = True; record['completed_utc'] = datetime.now(timezone.utc).isoformat(); save()
    print('Original pickle native capture terminal:',record['status'],flush=True)
    return 0 if record['status'] == 'terminal_unscored_native_location_diagnostic' else 1


if __name__ == '__main__': raise SystemExit(main())
