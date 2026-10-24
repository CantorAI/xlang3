"""Root-only bounded VM trial: fresh correctness, fixed gate and original JSON.

Old capture proofs keep their original pin maps. Only the three declared source
changes, two added fixtures and authenticated candidate Release are rebound.
Sequential fast JSON observations are unpaired; this adapter makes no gain or
full-suite claim and never commits, retries, rebuilds or changes engine files.
"""
import argparse
import copy
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import runpy
import statistics
import subprocess
import sys

ROOT = Path('D:/CantorAI/xlang3')
DATA = ROOT / 'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
BASELINE = ROOT / 'build-repro/Release'
CONTROL = ROOT / 'build-repro/controls/gc-generic-cycles-r7b-accepted-20261009'
CONTROL_SHA = 'aa2d9f715ad11e78ffa58024c10631c37e7d83d816c4a8cb0523f6469287547f'
PRODUCER = ROOT / 'scratch/performance/run-gc-r7b-all97-ownership-supplement-r3-proposed-20261009.py'
PRODUCER_SHA = '517113f30f85dc73c62434865e564e2bca6a21331ef988dc438f10ceb4416f53'
BUNDLE = ROOT / 'scratch/performance/gc-r7b-ownership-supplement-bundle-r3-proposed-20261009.json'
BUNDLE_SHA = '5525ddbd30ed6ae93c065298fbd3182ae37db1d3fc114d82422934fb2a8926eb'
BUILDER_SHA = '9848e12d9ec2716bc1c7994f514e128761fedf9c439d18fa19e23ebe091c0539'
UNDERLYING_BUILDER_SHA = '120b53c7ffbe3788abf3224c08e16b6a765db8fb99085164fb96a6aaf794c0c1'
HARNESS_CONTROLLER_SHA = '373e7d0b16b04d047810b853a5513667e2c4585770dce728f34c2312ada83e0e'
DEBUGPY = 'tests/cli/run_debugpy_launch_smoke.py'
BUILD_COMMAND_SHA = '2efd6c0b5662ca4cef35bd337077ecb3a13ad95e919c8e42ca4bcbd99a62dc8b'
CHANGED = {'src/executor/xlang_vm/xlang_vm_loop.cpp', 'tests/run_fixtures.py', 'tests/run_fixtures.ps1'}
ADDED = {'tests/fixtures/core/vm_active_code_view.py', 'tests/fixtures/expected/vm_active_code_view.out'}
PATCH_SHA = '4dfd157ff5f3b356cab010be278233644eb19843b8327cd5dd76a56634eeb6a9'
FIXTURE_SHA = '549b33469a72b797964045ebb5a9b7c9e99ec8fc45949d03e082a29e321505a1'
EXPECTED = ('active-code-self-replacement PASS\nactive-code-generator-replacement PASS\n'
            'active-code-trace-replacement PASS\n')


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def text(path):
    return Path(path).read_text(encoding='utf-8').replace('\r\n', '\n').rstrip()


def main():
    require(sys.version_info[:3] == (3, 14, 7) and not sys.flags.optimize
            and Path(sys.executable).resolve() == CP.resolve(), 'Use unoptimized fixed CPython3.14.7')
    parser = argparse.ArgumentParser(description=__doc__)
    for label in ('application', 'build', 'terminal-supplement'):
        parser.add_argument('--' + label, type=Path, required=True)
        parser.add_argument('--' + label + '-sha256', required=True)
    parser.add_argument('--prefix', required=True)
    args = parser.parse_args()
    require(re.fullmatch(r'vm-active-code-view-[A-Za-z0-9_-]+', args.prefix), 'Unsafe trial prefix')
    require(not any(DATA.glob(args.prefix + '*')), 'Existing evidence: no automatic retry/overwrite')
    output = DATA / (args.prefix + '-validation.json')
    record = {'status': 'preflight', 'terminal': False, 'phases': [], 'engine_commit_permitted': False,
              'controller_sha256': sha(__file__), 'scope': 'Fresh correctness and unchanged fixed gate; '
              'original JSON control/candidate sequential fast observations, unpaired. Selected source '
              'and unowned-dirty provenance; no clean-checkout, full97 or new speedup claim.'}
    pins = trees = base = None
    head = None

    def save():
        output.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8', newline='\n')

    def receipt(path, expected):
        path = path.resolve(strict=True)
        require(path.is_relative_to(DATA.resolve()) and re.fullmatch(r'[0-9a-f]{64}', expected)
                and sha(path) == expected, 'Receipt path/hash differs: ' + str(path))
        return path, read(path)

    try:
        save()
        app_path, app = receipt(args.application, args.application_sha256)
        build_path, build = receipt(args.build, args.build_sha256)
        ledger_path, ledger = receipt(args.terminal_supplement, args.terminal_supplement_sha256)
        require(sha(PRODUCER) == PRODUCER_SHA and sha(BUNDLE) == BUNDLE_SHA, 'Frozen guard bundle differs')
        spec = importlib.util.spec_from_file_location('vm_view_prospective_producer', PRODUCER)
        producer = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(producer)
        common, bundle, bundle_path = producer.load_common(BUNDLE, BUNDLE_SHA)
        base, report, summary = common.primitives()
        require(ledger['terminal'] and ledger['binding']['controller_sha256'] == PRODUCER_SHA
                and ledger['binding']['bundle_sha256'] == BUNDLE_SHA
                and common.identity(ledger['terminal_identity'], ledger), 'Supplement not terminal/identity-valid')
        original, old_results, evidence, _ = common.authenticate_original(
            ledger['original']['path'], ledger['original']['sha256'], base, report, summary)
        added = {p: h for p, h in evidence.items() if p not in original['pins']}
        added.update({str((ROOT / p).resolve()): h for p, h in bundle['file_sha256'].items()})
        added[str(bundle_path)] = BUNDLE_SHA
        claim_path = common.contained(DATA, ledger['exclusive_claim'])
        require(sha(claim_path) == ledger['exclusive_claim_sha256'], 'Supplement claim differs')
        claim = {'protocol': common.PROTOCOL, 'prefix': common.PREFIX, 'original': ledger['original'],
                 'new_controller_sha256': PRODUCER_SHA, 'bundle_sha256': BUNDLE_SHA,
                 'binding_digest': ledger['binding_digest']}
        require(read(claim_path) == claim and ledger['exclusive_claim'] == common.PREFIX + '-exclusive-origin-claim.json',
                'Supplement claim association differs')
        added[str(claim_path)] = ledger['exclusive_claim_sha256']
        require(ledger['added_pin_sha256'] == added and ledger['pins'] == {**original['pins'], **added},
                'Undeclared old-origin pin changes')
        common.authenticate_composite(ledger, original, old_results, base, summary, evidence)

        manifest_path = CONTROL / 'provenance.json'
        require(sha(manifest_path) == CONTROL_SHA, 'Accepted control manifest differs')
        control = read(manifest_path)
        require(control['terminal'] and control['full_validated'] and control['correctness_passed']
                and control['fixed_gate_passed'] and control['affected_originals_completed'], 'Control not accepted')
        require(ledger['binding']['source_sha256'] == control['source_sha256']
                and ledger['binding']['release_sha256'] == control['release_sha256']
                and ledger['binding']['baseline_sha256'] == control['fixed_baseline_sha256'], 'Old control lineage differs')
        require(app['terminal'] and Path(app['accepted_control_manifest_path']).resolve() == manifest_path.resolve()
                and app['accepted_control_manifest_sha256'] == CONTROL_SHA
                and Path(app['terminal_supplement_path']).resolve() == ledger_path
                and app['terminal_supplement_sha256'] == args.terminal_supplement_sha256, 'Application parent differs')
        require(set(app['allowed_changed_source_paths']) == CHANGED and set(app['added_source_paths']) == ADDED,
                'Application scope differs')
        sources = app['source_sha256']
        require(set(sources) == set(control['source_sha256']) | ADDED and app['source_count'] == len(sources)
                and {p for p, h in control['source_sha256'].items() if sources[p] != h} == CHANGED,
                'Unexpected source rebinding')
        require(app['fixed_baseline_sha256'] == control['fixed_baseline_sha256']
                and app['unowned_tracked_dirty_sha256'] == control['unowned_tracked_dirty_sha256'], 'Baseline/unowned changed')
        # The harness repair is its own already-passed layer, not an old engine
        # approval. Candidate PS1 also includes exactly one new registration.
        wrapper = ROOT / 'scratch/performance/build-vm-active-code-view-root-r2-20261009.py'
        require(sha(wrapper) == BUILDER_SHA, 'Root build wrapper differs')
        wrapper_spec = importlib.util.spec_from_file_location('vm_view_root_builder', wrapper)
        wrapper_module = importlib.util.module_from_spec(wrapper_spec)
        wrapper_spec.loader.exec_module(wrapper_module)
        harness_layer = wrapper_module.authenticate_harness(app, control)
        harness_path = Path(harness_layer['harness_repair_receipt_path']).resolve(strict=True)
        harness = read(harness_path)
        require(harness['status'] == 'full55_control_passed'
                and len(harness['registered_names']) == len(set(harness['registered_names'])) == 55
                and set(harness['registered_names']) == set(harness['passed_names']),
                'Harness repair was not complete55')
        committed_ps1 = subprocess.run(['git', 'show', app['head'] + ':tests/run_fixtures.ps1'],
                                       cwd=ROOT, capture_output=True, check=True).stdout
        require(hashlib.sha256(committed_ps1).hexdigest()
                == harness_layer['harness_repair_source_sha256']['tests/run_fixtures.ps1'],
                'Committed PS1 repair ancestor differs')
        normalized_ps1 = committed_ps1.replace(b'\r\n', b'\n')
        registration_anchor = b'    "gc_generic_cycles",\n'
        require(normalized_ps1.count(registration_anchor) == 1, 'Ambiguous PS1 registration anchor')
        require((ROOT / 'tests/run_fixtures.ps1').read_bytes().replace(b'\r\n', b'\n')
                == normalized_ps1.replace(registration_anchor,
                   registration_anchor + b'    "vm_active_code_view",\n', 1),
                'PS1 differs beyond the authenticated repair and registration')
        harness_evidence = {str(harness_path): harness_layer['harness_repair_receipt_sha256']}
        harness_controller = ROOT / 'scratch/performance/check-gc-r7b-full-ctest-harness-repair-20261009.py'
        require(sha(harness_controller) == harness['controller_sha256'] == HARNESS_CONTROLLER_SHA,
                'Passed harness controller differs')
        harness_evidence[str(harness_controller)] = HARNESS_CONTROLLER_SHA
        for label in ('stdout', 'stderr'):
            stream = base.contained(DATA, harness[label])
            require(sha(stream) == harness[label + '_sha256'], 'Passed harness stream differs')
            harness_evidence[str(stream)] = harness[label + '_sha256']
        require(not (DATA / harness['stderr']).read_bytes(), 'Passed harness stderr was not empty')
        passed_harness_names = re.findall(r'Test\s+#\d+:\s+(\S+)\s+.*?Passed', text(DATA / harness['stdout']))
        require(len(passed_harness_names) == 55 and set(passed_harness_names) == set(harness['registered_names'])
                and '100% tests passed, 0 tests failed out of 55' in text(DATA / harness['stdout']),
                'Raw harness output does not prove55')
        harness_inventory = DATA / 'gc-r7b-full-ctest-harness-repair-20261009-inventory.stdout.log'
        require(sha(harness_inventory) == harness['pins'][str(harness_inventory)], 'Harness inventory differs')
        old_inventory = read(harness_inventory)['tests']
        require(len(old_inventory) == 55 and {t['name'] for t in old_inventory} == set(harness['registered_names']),
                'Harness inventory names differ')
        harness_evidence[str(harness_inventory)] = sha(harness_inventory)
        require(DEBUGPY not in sources and set(app['protected_test_input_sha256']) == {DEBUGPY},
                'Protected debugpy input must be separate from source145')
        patch = Path(app['trial_patch_path']).resolve(strict=True)
        require(patch.is_relative_to((ROOT / 'scratch/performance').resolve())
                and sha(patch) == app['trial_patch_sha256'] == PATCH_SHA, 'Reviewed patch differs')
        fixture = ROOT / 'tests/fixtures/core/vm_active_code_view.py'
        expected = ROOT / 'tests/fixtures/expected/vm_active_code_view.out'
        require(sources[fixture.relative_to(ROOT).as_posix()] == sha(fixture) == FIXTURE_SHA
                and text(expected) == EXPECTED.rstrip(), 'Frozen fixture/transcript differs')
        require(build['terminal'] and build['passed'] and build['exit_code'] == 0 and build['sources_unchanged']
                and build['owned_child_cleanup_completed']
                and build['application_sha256'] == args.application_sha256 and build['source_sha256'] == sources
                and build['source_count'] == len(sources), 'Build not validated for this application')
        require(build.get('harness_layer_unchanged') is True
                and build.get('underlying_controller_sha256') == UNDERLYING_BUILDER_SHA
                and all(build.get(k) == v for k, v in harness_layer.items()), 'Build harness layer changed')
        release = build['release_sha256']
        require(set(release) == set(control['release_sha256']) and len(release) == 178, 'Release population differs')
        require(release['xlang3_runtime.dll'] != control['release_sha256']['xlang3_runtime.dll'], 'No fresh candidate DLL')
        head = app['head']
        require(re.fullmatch(r'[0-9a-f]{40}', head), 'Missing fresh application HEAD')

        # Preserve old proof maps intact. Rebind only explicitly owned source and
        # candidate-image paths in this NEW validation map, leaving every other
        # dependency, CP/gate/control/baseline pin unchanged.
        pins = dict(ledger['pins'])
        rebinding = {}
        for directory, values in ((ROOT, sources), (RELEASE, release),
                                  (ROOT, app['protected_test_input_sha256'])):
            for name, value in values.items():
                key = str((directory / name).resolve())
                if key in pins and pins[key] != value:
                    require(directory == RELEASE or name in CHANGED or name == DEBUGPY, 'Unowned pin rebinding: ' + key)
                    rebinding[key] = {'old': pins[key], 'candidate': value}
                elif key not in pins:
                    require(directory == ROOT and (name in ADDED or name == DEBUGPY), 'Unexpected new candidate pin')
                pins[key] = value
        for directory, values in ((BASELINE, control['fixed_baseline_sha256']),
                                  (CONTROL / 'sources', control['source_sha256']),
                                  (CONTROL / 'Release', control['release_sha256']),
                                  (ROOT, control['unowned_tracked_dirty_sha256'])):
            for name, value in values.items():
                key = str((directory / name).resolve())
                require(pins.get(key) == value, 'Immutable parent/unowned pin differs')
        evidence.update({str(app_path): args.application_sha256, str(build_path): args.build_sha256,
                         str(ledger_path): args.terminal_supplement_sha256, str(patch): PATCH_SHA,
                         str(Path(__file__).resolve()): sha(__file__)})
        for field in ('controller_sha256', 'build_command_source_sha256'):
            require(re.fullmatch(r'[0-9a-f]{64}', build[field]), 'Build provenance missing')
        require(build['controller_sha256'] == BUILDER_SHA and build['build_command_source_sha256'] == BUILD_COMMAND_SHA,
                'Reviewed build provenance differs')
        builder = wrapper
        underlying_builder = ROOT / 'scratch/performance/build-vm-active-code-view-proposed-20261009.py'
        evidence[str(underlying_builder)] = UNDERLYING_BUILDER_SHA
        evidence.update(harness_evidence)
        command_source = Path(build['command'][-1]).resolve(strict=True)
        require(build['command'] == ['cmd.exe', '/d', '/c', str(command_source)]
                and command_source.is_relative_to((ROOT / 'scratch/performance').resolve()), 'Build command differs')
        evidence[str(builder)] = build['controller_sha256']
        evidence[str(command_source)] = build['build_command_source_sha256']
        evidence[str(base.contained(DATA, build['log']))] = build['log_sha256']
        for p, h in evidence.items():
            require(p not in pins or pins[p] == h, 'Evidence would overwrite old pin')
            pins[p] = h
        trees = copy.deepcopy(ledger['input_trees'])
        trees['candidate_release']['sha256'] = release
        for label, directory in (('trial_core_fixtures', ROOT / 'tests/fixtures/core'),
                                 ('trial_compat_fixtures', ROOT / 'tests/fixtures/compat_sections'),
                                 ('trial_expected', ROOT / 'tests/fixtures/expected')):
            values = base.tree_hashes(directory, True)
            trees[label] = {'directory': str(directory.resolve()), 'exclude_bytecode': True, 'sha256': values}
            for name, value in values.items():
                key = str(base.contained(directory, name))
                require(key not in pins or pins[key] == value, 'Fixture tree would rebind an old source')
                pins[key] = value
        previous = read(Path(original['binding']['receipt_paths']['correctness']))
        old_phases = {p['name']: p for p in previous['phases']}
        gate_script = ROOT / 'benchmarks/check_regression.py'
        gate_cases = tuple(runpy.run_path(str(gate_script))['CASES'])
        require(len(set(gate_cases)) == 11, 'Default gate population differs')
        full = runpy.run_path(str(ROOT / 'tests/run_fixtures.py'))
        require(len(full['CORE_CASES']) == 407 and full['CORE_CASES'].count('vm_active_code_view') == 1
                and len(full['SECTION_CASES']) == 11, 'Registered fixture population differs')
        gate_sources = {name: sha(ROOT / 'benchmarks/cases' / (name + '.py')) for name in gate_cases}
        testfile = RELEASE.parent / 'CTestTestfile.cmake'
        generated = testfile.read_text(encoding='utf-8')
        ctest_names = set(re.findall(r'add_test\(\[=\[(.*?)\]=\]', generated))
        require(len(ctest_names) == 55, 'Complete generated CTest population differs')
        ctest = Path(old_phases['ctest']['command'][0]).resolve(strict=True)
        ctest_base = [str(ctest), '--test-dir', str(RELEASE.parent), '-C', 'Release']
        correctness_commands = {name: old_phases[name]['command'] for name in
                                ('full-fixtures-clean', 'old_xlang_sqlite_api', 'python_sqlite3_api')}
        correctness_commands['full-ctest-inventory'] = ctest_base + ['--show-only=json-v1']
        correctness_commands['full-ctest'] = ctest_base + ['--output-on-failure']
        # Pin the generated registration and its explicitly referenced scripts
        # before inventory execution. Configuration-specific non-Release binary
        # paths are not executed; actual JSON commands are checked/pinned below.
        referenced_scripts = set(re.findall(r"[A-Za-z]:[/\\][^'\";()\s]+\.(?:py|ps1|cmake|out)\b", generated))
        for p in (testfile, *(Path(v) for v in referenced_scripts)):
            key = str(p.resolve(strict=True)); value_sha = sha(p)
            require(key not in pins or pins[key] == value_sha, 'Generated CTest/script pin conflicts')
            pins[key] = value_sha
        for value in set(re.findall(r'"([A-Za-z]:[/\\][^"]+\.exe)"', generated)):
            p = Path(value).resolve()
            if p.is_relative_to(RELEASE.parent.resolve()) and not p.is_relative_to(RELEASE.resolve()):
                continue  # Other CMake configurations are not the selected Release inventory.
            require(p.is_file(), 'Generated Release/external executable is missing: ' + value)
            key = str(p); value_sha = sha(p)
            require(key not in pins or pins[key] == value_sha, 'CTest executable pin conflicts')
            pins[key] = value_sha
        for command in correctness_commands.values():
            for value in command:
                p = Path(value)
                if p.is_absolute() and p.is_file():
                    key = str(p.resolve()); value_sha = sha(p)
                    require(key not in pins or pins[key] == value_sha, 'Command input changed')
                    pins[key] = value_sha
        for p in (gate_script, base.RUNNER, base.HOOK, base.WATCH):
            key = str(p.resolve()); require(key in pins and sha(p) == pins[key], 'Guard/runner not pinned')
        record.update(status='validating', head=head, original_head=ledger['binding']['head'],
                      application_sha256=args.application_sha256, build_sha256=args.build_sha256,
                      terminal_supplement_sha256=args.terminal_supplement_sha256, source_count=len(sources),
                      harness_repair=harness_layer, protected_test_input_sha256=app['protected_test_input_sha256'],
                      harness_repair_scope='Separate untimed pretrial repair passed all55; candidate full55 remains fresh below.',
                      fixture_counts={'core': 407, 'compat_sections': 11, 'expected_failures': 3},
                      complete_ctest_count=55, generated_ctest_file=str(testfile), generated_ctest_sha256=sha(testfile),
                      ctest_referenced_script_sha256={str(Path(v).resolve()): sha(v) for v in referenced_scripts},
                      historical_parent_ctest_scope='Selected nine only; this candidate requires all 55 without waivers.',
                      pins=pins, input_trees=trees, explicit_rebinding=rebinding,
                      original_binding_digest=original['binding_digest'], supplement_binding_digest=ledger['binding_digest'])
        base.OwnedProcesses = producer.ownership_class(base)
        env = os.environ.copy()
        for key in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING', 'PYTHONOPTIMIZE',
                    'PYTHONHOME', 'XLANG3_VM_OPCODE_TIMING'):
            env.pop(key, None)
        env['XLANG3_PYTHON_LIB'] = str(CP.parent / 'Lib')

        def phase(name, command, timeout, expected_stdout=None, check=None, ctest=False):
            row = {'name': name, 'command': list(map(str, command)), 'timeout': timeout,
                   'valid': False, 'timing_scoring_permitted': False, 'untimed_ctest': ctest}
            record['phases'].append(row); save()
            watcher, owned, admission, classify = base.make_watcher(args.prefix, name, None, None)
            row.update(admission=admission, manager_identity=owned.manager)
            child = finish = None
            stdout, stderr = (DATA / (args.prefix + '-' + name + suffix) for suffix in ('.stdout.log', '.stderr.log'))
            def idle():
                observed = classify(watcher.scan(), manager_only=True)
                return {'passed': not observed['busy'], **observed}
            try:
                row['pre_idle'] = idle(); require(row['pre_idle']['passed'], 'Foreign activity before ' + name)
                row['pre_identity'] = base.stable(pins, trees, head)
                require(row['pre_identity']['passed'], 'Identity changed before ' + name)
                row['launch_idle'] = idle(); require(row['launch_idle']['passed'], 'Foreign activity at launch ' + name)
                finish = watcher.start_timing_process_watch(args.prefix, name, row)
                with stdout.open('xb') as out, stderr.open('xb') as err:
                    child = subprocess.Popen(row['command'], cwd=ROOT, env=env, stdin=subprocess.DEVNULL,
                                             stdout=out, stderr=err, creationflags=subprocess.CREATE_NO_WINDOW)
                    owned.seed_child(child, watcher.scan()); row['child_identity'] = owned.child_identity
                    row['pid'] = child.pid; save(); row['exit_code'] = child.wait(timeout=timeout)
            except BaseException as error:
                row['error'] = repr(error)
            finally:
                try:
                    if child is not None and child.poll() is None:
                        subprocess.run(['taskkill', '/F', '/T', '/PID', str(child.pid)], capture_output=True, timeout=10, check=False)
                        if child.poll() is None: child.kill()
                        child.wait(timeout=10)
                    row['cleanup_passed'] = child is None or child.poll() is not None
                except BaseException as error:
                    row['cleanup_error'] = repr(error)
                for key, action in (('measurement_valid', lambda: finish() if finish else False),
                                    ('post_idle', idle), ('post_identity', lambda: base.stable(pins, trees, head)),
                                    ('observed_owned_process_identities', owned.snapshot)):
                    try: row[key] = action()
                    except BaseException as error: row[key + '_error'] = repr(error)
                try:
                    row['raw_sha256'] = {str(p): sha(p) for p in (stdout, stderr) if p.is_file()}
                    watch = row.get('external_process_watch', {})
                    if watch.get('log'): row['raw_sha256'][str(DATA / watch['log'])] = sha(DATA / watch['log'])
                    require(not watch.get('log') or row['raw_sha256'][str(DATA / watch['log'])] == watch['sha256'], 'Watch log changed')
                except BaseException as error:
                    row['raw_hash_error'] = repr(error)
                save()
            # Keep raw negative watcher evidence. Only an owned, creation-pinned
            # CTest root can receive this UNTIMED semantic allowance; timing
            # phases always require the original strict watcher to accept.
            watch = row.get('external_process_watch', {})
            overlaps = watch.get('overlaps', [])
            own = row.get('child_identity') or {}
            ctest_only = (ctest and overlaps and not watch.get('scanner_errors')
                          and own.get('Name', '').lower() == 'ctest.exe'
                          and all(o.get('busy') and all(base.process_identity(p) == own for p in o['busy']) for o in overlaps))
            row['untimed_owned_ctest_only'] = bool(ctest_only)
            activity = row.get('measurement_valid') is True or bool(ctest_only)
            row['valid'] = (not any(k == 'error' or k.endswith('_error') for k in row)
                            and row.get('exit_code') == 0 and row.get('cleanup_passed', False) and activity
                            and row.get('post_idle', {}).get('passed', False)
                            and row.get('post_identity', {}).get('passed', False))
            save(); require(row['valid'], 'Failed/invalid phase ' + name)
            require(not text(stderr), 'Unexpected stderr in ' + name)
            if expected_stdout is not None: require(text(stdout) == expected_stdout.rstrip(), 'Transcript differs in ' + name)
            if check is not None: check(stdout)
            row['semantic_output_passed'] = True; save()
            print(name + ': PASS', flush=True)
            return row

        phase('cpython-active-code-view', [CP, '-I', fixture], 120, EXPECTED)
        phase('xlang3-active-code-view', [RELEASE / 'xlang3.exe', fixture], 120, EXPECTED)
        inventory_names = set()
        def inventory_check(path):
            tests = read(path)['tests']
            names = {p['name'] for p in tests}
            require(len(tests) == 55 and names == ctest_names, 'Unfiltered CTest inventory differs')
            for test in tests:
                command = test['command']
                require(command and not any('python313' in str(v).lower() for v in command), 'Stale Python3.13 CTest command')
                exe = Path(command[0]).resolve(strict=True)
                if exe.name.lower().startswith('python'):
                    require(exe == CP.resolve(), 'CTest Python interpreter differs')
                for value in command:
                    p = Path(value)
                    if p.is_absolute() and p.is_file():
                        key = str(p.resolve()); value_sha = sha(p)
                        require(pins.get(key) == value_sha, 'CTest command was not pinned before inventory')
            inventory_names.update(names)
            record['full_ctest_commands'] = {test['name']: test['command'] for test in tests}
        def ctest_check(path):
            value = text(path)
            passed = re.findall(r'Test\s+#\d+:\s+(\S+)\s+.*?Passed', value)
            require(len(passed) == 55 and set(passed) == inventory_names
                    and '100% tests passed, 0 tests failed out of 55' in value, 'Full 55 CTests did not pass')
        for name in ('full-fixtures-clean', 'full-ctest-inventory', 'full-ctest', 'old_xlang_sqlite_api', 'python_sqlite3_api'):
            check = inventory_check if name == 'full-ctest-inventory' else ctest_check if name == 'full-ctest' else None
            expected_text = None if check else text(DATA / old_phases[name]['stdout'])
            phase(name, correctness_commands[name], 1800 if name == 'full-ctest' else 600,
                  expected_text, check, name.startswith('full-ctest'))
        record['correctness_passed'] = True; save()
        gate_output = DATA / (args.prefix + '-fixed-gate.json')
        phase('fixed-gate', [CP, gate_script, '--baseline', BASELINE / 'xlang3.exe', '--candidate', RELEASE / 'xlang3.exe',
                             '--repeats', '21', '--warmup', '5', '--threshold', '0.10', '--output', gate_output], 900)
        gate = read(gate_output)
        require(gate['status'] == 'pass' and (gate['repeats'], gate['warmup'], gate['threshold']) == (21, 5, .10)
                and set(gate['cases']) == set(gate_cases)
                and all(v['status'] == 'pass' and v['source_sha256'] == gate_sources[n] for n, v in gate['cases'].items()),
                'Fixed gate failed/weakened')
        record['fixed_gate'] = {'passed': True, 'output': str(gate_output), 'sha256': sha(gate_output)}; save()
        env.update(PYTHONPATH=str(ROOT / 'benchmarks/diagnostics/pyperf_compat'), PYTHONIOENCODING='utf-8')
        scores = {}
        for label, exe in (('control', CONTROL / 'Release/xlang3.exe'), ('candidate', RELEASE / 'xlang3.exe')):
            result = DATA / (args.prefix + '-' + label + '-json-dumps-fast.json')
            phase(label + '-json-dumps', [CP, base.RUNNER, '--runtime', exe, '--benchmarks', 'json_dumps', '--mode', 'fast',
                                         '--case-timeout', '300', '--dependency-site', base.SITE, '--output', result], 360)
            raw = read(result); benchmarks = summary.benchmark_map(raw)
            require(set(benchmarks) == {'json_dumps'}, 'Original JSON subtest differs')
            benchmark = benchmarks['json_dumps']; values = summary.values(benchmark)
            require({**raw.get('metadata', {}), **benchmark.get('metadata', {})}.get('unit') == 'second'
                    and len(values) == 20 and all(math.isfinite(v) and v > 0 for v in values), 'JSON scored population differs')
            scores[label] = {'output': str(result), 'sha256': sha(result), 'values': values,
                             'mean_seconds': statistics.fmean(values), 'stdev_seconds': statistics.stdev(values)}
            record['original_json'] = scores; save()
        record.update(status='correctness_gate_and_original_json_completed',
                      unpaired_control_over_candidate=scores['control']['mean_seconds'] / scores['candidate']['mean_seconds'])
    except BaseException as error:
        record.update(status='validation_failed_or_invalid', error=repr(error))
    finally:
        if base is not None and pins is not None and trees is not None and head is not None:
            try: record['terminal_identity'] = base.stable(pins, trees, head)
            except BaseException as error: record['terminal_guard_error'] = repr(error)
        record['terminal'] = True
        if not record.get('terminal_identity', {}).get('passed', False): record['status'] = 'validation_failed_or_invalid'
        save()
    print(json.dumps({'status': record['status'], 'receipt': str(output), 'receipt_sha256': sha(output)}, indent=2), flush=True)
    return 0 if record['status'] == 'correctness_gate_and_original_json_completed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
