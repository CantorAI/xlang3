"""One validated Python-new VM continuation R4 all-97 attempt, reusing historical October 7 CPython 3.14.7.

Prepared only. Root launches only after the actual same-R4 full correctness/default-gate/unpickle validation supplied by CLI hash.
No build, retry, benchmark selection reduction or CPython full rerun is performed.
"""
from __future__ import annotations
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import importlib.metadata
import importlib.util
import json
import math
import os
import statistics
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
CANDIDATE = ROOT / 'build-repro/main-verify-20261006/Release/xlang3.exe'
CP_STEM = 'pyperformance-cpython3147-live-eval-full-fast-20261007'
CP_HASHES = {'.json': 'ac474df5576ac85f6f5350363affecc439d4f658b4dfa3dbe291b46964276eb0',
    '.log': '6a1824eed3abad4cebae80356f0f600f1a8728755730d2f64c0bd7d4b3ca9e6c',
    '-provenance.json': '3d6c8cb2c72c786583c382b835e8e411f56ca285963bc2835f757a13f399f85c'}
BENCHMARK_ROOT = CP.parent / 'Lib/site-packages/pyperformance/data-files/benchmarks'
HOOK = ROOT / 'benchmarks/diagnostics/pyperf_compat/sitecustomize.py'
RUNNER = ROOT / 'benchmarks/diagnostics/run_pyperformance_xlang3_shimmed.py'
SUMMARY = ROOT / 'benchmarks/diagnostics/summarize_pyperformance_comparison.py'
PARTIAL_HELPER = RUNNER.with_name('preserve_pyperformance_partial.py')
WATCH = ROOT / 'scratch/performance/validate-call-ex-cross-activation-constructor-resume-r3-20261008.py'
WATCH_SHA = '50007db5cc6adc2e54012fdfbd48514699513f990906d4a508582ba73d6ae0bf'
INVENTORY = DATA / 'python-new-vm-continuation-r4-applied-source-20261009.json'
INVENTORY_SHA = '2c89133fbe3c3a4c0d13a1652b62362d69be1c79b07e197186682eac664cb0a8'
BUILD = DATA / 'python-new-vm-continuation-r4-build-20261009.json'
BUILD_SHA = 'd4dc98f0c23e1d2b902a6a658cc1d7211c59ca01caa8f10a4a95ffb5d67644b9'
FOCUSED = DATA / 'python-new-vm-continuation-r4-focused-20261009.json'
FOCUSED_SHA = '550e4815da236e087723e1eca72e2772f50d05fbc247399a25fd44d95ace8793'
VALIDATOR = ROOT / 'scratch/performance/validate-python-new-vm-continuation-r4-trial-proposed-20261009.py'
VALIDATOR_SHA = '8b4e99e85597ce4565fe792a0b24c6e77ad47ca61ac931830721182370aefcd0'
CURRENT_HEAD = '4b1cfefc80f0bbb9e1f60c09c46d83619ded0e17'
ACCEPTED_BASE = 'b188b24e86eac9cbc10c7f0efe6234d98caa0009'
ACCEPTED_MANIFEST = ROOT / 'build-repro/controls/lambda-eager-comprehension-capture-accepted-r5-20261009/preserved-release-provenance.json'
ACCEPTED_MANIFEST_SHA = '9271314856ede7f173396b2f39449a01d785f61a1579ce5f93c7786a2961dfad'
RELEASE = CANDIDATE.parent
BASELINE = ROOT / 'build-repro/Release'


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def document(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def source_map(row):
    if 'source_sha256' in row:
        return row['source_sha256']
    return {name: value['working_sha256'] for name, value in row['files'].items()}


def normalize_release(mapping):
    # Preserve the corrected R5 map boundary: exactly one fixed Release file
    # per input key, accepting existing Release- or repository-relative keys.
    normalized = {}
    for name, value in mapping.items():
        relative = Path(name)
        assert not relative.is_absolute() and '..' not in relative.parts
        assert re.fullmatch(r'[0-9a-f]{64}', value)
        repo_path = (ROOT / relative).resolve()
        path = repo_path if repo_path.is_relative_to(RELEASE.resolve()) else (RELEASE / relative).resolve()
        assert path.is_relative_to(RELEASE.resolve())
        key = path.relative_to(ROOT.resolve()).as_posix()
        assert key not in normalized
        normalized[key] = value
    return normalized

def tree_inputs(directory):
    # Hash existing installed source/data/native bytes, excluding only bytecode.
    # Historical CP provenance pins METADATA, not all these historical bytes.
    return {p.relative_to(directory).as_posix(): digest(p) for p in sorted(directory.rglob('*'))
        if p.is_file() and '__pycache__' not in p.parts and p.suffix not in ('.pyc', '.pyo')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--validation', type=Path, required=True)
    parser.add_argument('--validation-sha256', required=True, help='Actual future passed full-validation receipt hash; never guessed')
    parser.add_argument('--source-inventory', type=Path, required=True)
    parser.add_argument('--head', required=True, help='Exact actual final 40-hex HEAD, supplied by root')
    parser.add_argument('--accepted-base', default=ACCEPTED_BASE)
    parser.add_argument('--prefix', default='pyperformance-xlang3-python-new-r4-full-fast-20261009')
    parser.add_argument('--canonical-status', type=Path,
        default=DATA / 'pyperformance-xlang3-live-eval-vs-cpython3147-full-fast-20261007-all-97-status.csv')
    args = parser.parse_args()
    assert sys.version_info[:3] == (3, 14, 7) and Path(sys.executable).resolve() == CP.resolve() and not sys.flags.optimize
    assert re.fullmatch(r'[0-9a-f]{40}', args.head), 'Supply the exact final commit, not a moving ref'
    assert re.fullmatch(r'[a-z0-9-]+', args.prefix)
    assert not any(DATA.glob(args.prefix + '*')), 'Preserve earlier evidence; use a fresh prefix'
    output = DATA / (args.prefix + '.json')
    log_path = DATA / (args.prefix + '.log')
    provenance = DATA / (args.prefix + '-provenance.json')
    validation_path, inventory_path = (p.resolve(strict=True) for p in (args.validation, args.source_inventory))
    assert validation_path.is_relative_to(DATA.resolve())
    assert re.fullmatch(r'[0-9a-f]{64}', args.validation_sha256)
    assert digest(validation_path) == args.validation_sha256
    assert inventory_path == INVENTORY.resolve() and digest(inventory_path) == INVENTORY_SHA
    validation, inventory = document(validation_path), document(inventory_path)
    assert validation['terminal'] and validation['hashes_unchanged'] and validation['full_validated']
    assert validation['status'] == 'trial_validated' and validation['hashes_before'] == validation['hashes_after']
    assert validation['controller_sha256'] == VALIDATOR_SHA and digest(VALIDATOR) == VALIDATOR_SHA
    assert validation['correctness_passed'] and not validation['official_failures']
    assert validation['fixture_counts'] == {'core': 403, 'compatibility_sections': 11, 'expected_failure_cases': 3}
    assert validation['ctest_count'] == 9 and validation['native_api_checks'] == 2
    assert digest(BUILD) == validation['build_receipt_sha256'] == BUILD_SHA
    assert digest(FOCUSED) == validation['focused_receipt_sha256'] == FOCUSED_SHA
    assert digest(ACCEPTED_MANIFEST) == validation['parent_manifest_sha256'] == ACCEPTED_MANIFEST_SHA
    build, focused, accepted = map(document, (BUILD, FOCUSED, ACCEPTED_MANIFEST))
    sources = source_map(inventory)
    assert inventory['terminal'] and len(sources) == inventory['source_count'] == 132
    assert sources == validation['source_sha256'] == build['source_sha256'] == focused['source_sha256']
    assert validation['source_count'] == build['source_count'] == focused['source_count'] == 132
    assert validation['source_inventory_sha256'] == build['application_sha256'] == focused['application_sha256'] == INVENTORY_SHA
    assert build['status'] == 'build_passed' and build['terminal'] and build['passed'] and build['exit_code'] == 0
    assert not build['timed'] and build['source_unchanged'] and build['accepted_control_unchanged'] and build['fixed_baseline_unchanged']
    assert build['hashes_before'] == build['hashes_after']
    assert focused['status'] == 'focused_passed' and focused['terminal'] and focused['passed'] and focused['hashes_unchanged']
    assert not focused['timed'] and focused['hashes_before'] == focused['hashes_after']
    assert focused['build_sha256'] == BUILD_SHA and len(focused['phases']) == len(focused['expected_names']) == 19
    assert [row['name'] for row in focused['phases']] == focused['expected_names']
    assert all(row['passed'] and row['exit_code'] == 0 for row in focused['phases'])
    assert validation['focused_phase_names'] == focused['expected_names']
    assert validation['focused_controller_sha256'] == focused['controller_sha256']
    assert accepted['status'] == 'preserved_fully_validated_lambda_capture_r5' and accepted['terminal']
    assert accepted['full_validated'] and accepted['correctness_passed'] and accepted['fixed_gate_passed']
    assert accepted['head'] == ACCEPTED_BASE and not accepted['remaining_official_sql_failures']
    assert accepted['validation_sha256'] == '6a41ee3fe92453d6472f2bb1c24b2da7ef125ac8425d27efdffa9f2262ab2d35'
    assert (accepted['source_count'], accepted['file_count']) == (128, 178)
    assert set(accepted['source_snapshot_sha256']) <= set(sources)
    binaries = normalize_release(focused['binaries_sha256'])
    assert len(binaries) == build['binary_file_count'] == 178
    assert binaries == normalize_release(build['binaries_sha256']) == normalize_release(validation['binaries_sha256'])
    candidate_binary_sha256 = {'exe': build['exe_sha256'], 'dll': build['dll_sha256']}
    assert validation['candidate_binary_sha256'] == candidate_binary_sha256
    assert digest(CANDIDATE) == candidate_binary_sha256['exe']
    assert digest(CANDIDATE.with_name('xlang3_runtime.dll')) == candidate_binary_sha256['dll']
    baseline = accepted['fixed_baseline_sha256']
    assert len(baseline) == 177 and baseline == validation['baseline_sha256']
    accepted_release = ACCEPTED_MANIFEST.parent / 'Release'
    accepted_sources = ACCEPTED_MANIFEST.parent / 'source-snapshot'
    relative_tree = lambda directory: {p.relative_to(directory).as_posix(): digest(p) for p in sorted(directory.rglob('*')) if p.is_file()}
    raw_tree = lambda directory: {p.relative_to(ROOT).as_posix(): digest(p) for p in sorted(directory.rglob('*')) if p.is_file()}
    assert relative_tree(accepted_release) == accepted['files_sha256']
    assert relative_tree(accepted_sources) == accepted['source_snapshot_sha256']
    assert raw_tree(RELEASE) == binaries and relative_tree(BASELINE) == baseline
    assert all(re.fullmatch(r'[0-9a-f]{64}', value) for value in sources.values())
    # Complete same-source untimed semantics are prerequisites only; their
    # retained compiler/CTest watch flags do not admit this new timed run.
    correctness_names = ['full-fixtures-clean', 'ctest-inventory', 'ctest', 'old_xlang_sqlite_api', 'python_sqlite3_api']
    assert [row['name'] for row in validation['phases'][:5]] == correctness_names
    assert len(validation['phases']) == 8
    for row in validation['phases'][:5]:
        assert row['passed'] and row['semantic_passed'] and row['exit_code'] == 0 and not row['timeout']
        assert not row['timed'] and not row['timing_accepted']
        assert row['owned_child_cleanup_completed'] and row['post_idle_guard_passed'] and row['post_hashes_stable']
    correctness_path = None
    if validation['correctness_reused_same_candidate']:
        prior_sha = validation['historical_correctness_receipt_sha256']
        matches = [Path(p) for p,h in validation['hashes_before'].items() if h == prior_sha and Path(p).suffix == '.json']
        assert len(matches) == 1
        correctness_path = matches[0].resolve(strict=True)
        assert digest(correctness_path) == prior_sha
        prior = document(correctness_path)
        assert prior['status'] == 'correctness_passed_performance_pending' and prior['terminal'] and prior['correctness_passed']
        assert prior['hashes_unchanged'] and prior['hashes_before'] == prior['hashes_after']
        assert prior['controller_sha256'] == VALIDATOR_SHA and prior['source_sha256'] == sources
        assert normalize_release(prior['binaries_sha256']) == binaries and prior['baseline_sha256'] == baseline
        assert len(prior['phases']) == 5
        for old,row in zip(prior['phases'],validation['phases'][:5]):
            assert row['executed_again'] is False and row['prior_receipt_sha256'] == prior_sha
            assert row['name'] == old['name'] and row['command'] == old['command']
            assert row['stdout_sha256'] == old['stdout_sha256'] and row['stderr_sha256'] == old['stderr_sha256']
    # Both original pure-Python unpickle20 attempts must be complete before
    # all97. These targeted rows never replace the historical full97 CP data.
    official_paths = []
    for runtime in ('cpython3147','xlang3'):
        name = 'official-'+runtime+'-unpickle_pure_python'
        row = next(row for row in validation['phases'] if row['name'] == name)
        assert row['passed'] and row['exit_code'] == 0 and not row['timeout'] and row['timed'] and row['timing_accepted']
        assert row['measurement_valid'] and row['owned_child_cleanup_completed'] and row['post_idle_guard_passed'] and row['post_hashes_stable']
        watch = row['external_process_watch']
        assert watch['measurement_valid'] and not watch['overlaps'] and not watch['scanner_errors']
        result = validation['official_results']['unpickle_pure_python'][runtime]
        attempt = validation['official_attempts'][name]
        assert attempt['complete'] and attempt['sha256'] == result['sha256'] and result['values_count'] == 20
        path = DATA/result['output']
        assert digest(path) == result['sha256']
        raw = document(path)
        assert len(raw['benchmarks']) == 1
        bench = raw['benchmarks'][0]
        metadata = dict(raw.get('metadata',{}), **bench.get('metadata',{}))
        assert metadata['name'] == 'unpickle_pure_python' and metadata['unit'] == 'second'
        assert metadata['pickle_module'] == 'pickle' and metadata['pickle_protocol'] == '5' and metadata['inner_loops'] == 20
        values = [value for run in bench['runs'] for value in run.get('values',[])]
        assert len(values) == 20 and all(math.isfinite(value) and value > 0 for value in values)
        assert statistics.mean(values) == result['mean_seconds']
        official_paths.append((path,result['sha256']))
    resolved_head = subprocess.check_output(['git', 'rev-parse', '--verify', args.head], cwd=ROOT, text=True).strip()
    actual_head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    assert actual_head == resolved_head == CURRENT_HEAD, 'Current R4 trial HEAD differs from supplied exact4b HEAD'
    accepted_base = subprocess.check_output(['git', 'rev-parse', '--verify', args.accepted_base], cwd=ROOT, text=True).strip()
    assert accepted_base == ACCEPTED_BASE, 'Accepted source base must be the frozen R5 checkpoint'
    subprocess.run(['git', 'merge-base', '--is-ancestor', accepted_base, actual_head], cwd=ROOT, check=True)
    spec = importlib.util.spec_from_file_location('full_refresh_summary', SUMMARY)
    summary = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(summary)
    with args.canonical_status.open(encoding='utf-8-sig', newline='') as stream:
        definitions = [row['benchmark'] for row in csv.DictReader(stream)]
    assert len(definitions) == len(set(definitions)) == 97
    cp_paths = {suffix: DATA / (CP_STEM + suffix) for suffix in CP_HASHES}
    assert all(digest(cp_paths[suffix]) == expected for suffix, expected in CP_HASHES.items())
    cp = document(cp_paths['-provenance.json'])
    assert cp['status'] == 'finished' and cp['exit_code'] == 0 and cp['runtime_version'] == '3.14.7'
    assert Path(cp['runtime_executable']).resolve() == CP.resolve()
    assert cp['expected_definitions'] == cp['attempted_definitions'] == 97 and not cp['failed_definitions']
    assert cp['recorded_subtests'] == 124 and len(summary.benchmark_map(document(cp_paths['.json']))) == 124
    assert set(summary.case_sections(cp_paths['.log'].read_text(encoding='utf-8'))) == set(definitions)
    assert cp['sha256_start'] == cp['sha256_end']
    assert cp['mode'] == 'fast' and cp['benchmarks'] == 'all'
    assert cp['case_timeout_seconds'] == 300 and cp['case_timeout_overrides'] == {'networkx*': 600}
    assert importlib.metadata.version('pyperformance') == cp['pyperformance_version']
    assert importlib.metadata.version('pyperf') == cp['pyperf_version']
    dependency_site = Path(cp['dependency_site']).resolve(strict=True)
    metadata = {p.relative_to(dependency_site).as_posix(): digest(p)
        for p in sorted(dependency_site.glob('*.dist-info/METADATA'))}
    expected_metadata = {name.replace('\\', '/'): value for name, value in cp['dependency_metadata_sha256'].items()}
    assert metadata == expected_metadata, 'Installed dependency metadata differs from the saved CP reference'
    benchmark_python = {p.relative_to(BENCHMARK_ROOT).as_posix(): digest(p)
        for p in sorted(BENCHMARK_ROOT.rglob('*.py'))}
    assert benchmark_python == {name.replace('\\', '/'): value for name, value in cp['benchmark_python_sources'].items()}
    assert digest(HOOK) == cp['compatibility_hook_sha256'] and digest(RUNNER) == cp['runner_sha256']
    assert digest(CP) == cp['sha256_end']['exe'] and digest(CP.with_name('python314.dll')) == cp['sha256_end']['dll']
    gate_meta = validation['fixed_gate']
    gate_path = DATA / gate_meta['output']
    assert gate_meta['exit_code'] == 0 and digest(gate_path) == gate_meta['sha256']
    gate = document(gate_path)
    assert gate['status'] == 'pass' and len(gate['cases']) == 11
    assert (gate['repeats'], gate['warmup'], gate['threshold']) == (21, 5, .1)
    assert all(case['status'] == 'pass' for case in gate['cases'].values())
    gate_row = next(row for row in validation['phases'] if row['name'] == 'fixed-gate')
    assert gate_row['passed'] and gate_row['exit_code'] == 0 and not gate_row['timeout']
    assert gate_row['timed'] and gate_row['timing_accepted'] and gate_row['measurement_valid']
    assert gate_row['owned_child_cleanup_completed'] and gate_row['post_idle_guard_passed'] and gate_row['post_hashes_stable']
    gate_watch = gate_row['external_process_watch']
    assert gate_watch['measurement_valid'] and not gate_watch['overlaps'] and not gate_watch['scanner_errors']
    tracked = {}

    def track(path, expected=None):
        path = Path(path).resolve(strict=True)
        value = digest(path)
        if expected is not None: assert value == expected, str(path)
        assert str(path) not in tracked or tracked[str(path)] == value
        tracked[str(path)] = value
        return value

    for path in (validation_path, inventory_path, args.canonical_status, gate_path, Path(__file__),
                 BUILD, FOCUSED, VALIDATOR, ACCEPTED_MANIFEST, HOOK, RUNNER, SUMMARY, PARTIAL_HELPER,
                 CP, CP.with_name('python314.dll'), *cp_paths.values()):
        track(path)
    track(WATCH, WATCH_SHA)
    for path, value in sources.items(): track(ROOT / path, value)
    for path, value in binaries.items(): track(ROOT / path, value)
    for path, value in baseline.items(): track(BASELINE / path, value)
    for path, value in accepted['files_sha256'].items(): track(accepted_release / path, value)
    for path, value in accepted['source_snapshot_sha256'].items(): track(accepted_sources / path, value)
    for path, value in validation['hashes_before'].items(): track(path, value)
    for path, value in inventory['unowned_tracked_dirty_sha256'].items(): track(ROOT / path,value)
    for receipt in (build,focused):
        for path,value in receipt['hashes_after'].items(): track(path,value)
    track(DATA/build['log'],build['log_sha256'])
    for path,value in official_paths: track(path,value)
    if correctness_path is not None: track(correctness_path,validation['historical_correctness_receipt_sha256'])
    for row in focused['phases']:
        for stream in ('stdout','stderr'): track(DATA/row[stream],row[stream+'_sha256'])
    for row in validation['phases']:
        for stream in ('stdout','stderr'): track(DATA/row[stream+'_log'],row[stream+'_sha256'])
        watch = row['external_process_watch']
        track(DATA/watch['log'],watch['sha256'])
    watch_spec = importlib.util.spec_from_file_location('python_new_r4_full97_watch', WATCH)
    watcher = importlib.util.module_from_spec(watch_spec)
    watch_spec.loader.exec_module(watcher)
    assert str(CANDIDATE.resolve()) in tracked and str(CANDIDATE.with_name('xlang3_runtime.dll').resolve()) in tracked
    record = {'status': 'preflight', 'terminal': False, 'pid': os.getpid(),
        'started_utc': datetime.now(timezone.utc).isoformat(), 'source_base_commit': actual_head,
        'accepted_base_commit': accepted_base, 'source_inventory': str(inventory_path),
        'source_inventory_sha256': digest(inventory_path), 'source_sha256': sources,
        'validation': str(validation_path), 'validation_sha256': digest(validation_path),
        'current_trial_status': validation['status'], 'current_correctness_receipt': str(correctness_path) if correctness_path else str(validation_path),
        'current_correctness_receipt_sha256': validation.get('historical_correctness_receipt_sha256',args.validation_sha256),
        'current_build_receipt': str(BUILD), 'current_build_receipt_sha256': BUILD_SHA, 'current_focused_receipt': str(FOCUSED),
        'current_focused_receipt_sha256': FOCUSED_SHA, 'binaries_sha256': binaries,
        'baseline_sha256': baseline, 'process_watch_source_sha256': WATCH_SHA,
        'accepted_control_manifest': str(ACCEPTED_MANIFEST), 'accepted_control_manifest_sha256': ACCEPTED_MANIFEST_SHA,
        'current_source_count': 132, 'current_fixture_counts': validation['fixture_counts'],
        'source_identity_scope': '132 recorded source inputs plus complete Release178 and fixed baseline177 bytes; not every compiled or transitive repository source, and not a clean-checkout build claim',
        'targeted_unpickle_validation_scope': 'The fresh original CP/X unpickle20 attempts validate the current R4 checkpoint only; they and date component diagnostics are not substituted into the historical all-97 reference',
        'fixed_gate': gate_path.name, 'fixed_gate_exit_code': 0, 'fixed_gate_sha256': digest(gate_path),
        'runtime_executable': str(CANDIDATE), 'runtime_version': 'XLang3',
        'manager_executable': str(CP), 'manager_version': sys.version,
        'pyperformance_version': cp['pyperformance_version'], 'pyperf_version': cp['pyperf_version'],
        'mode': 'fast', 'benchmarks': 'all', 'expected_definitions': 97,
        'case_timeout_seconds': 300, 'case_timeout_overrides': {'networkx*': 600},
        'benchmark_python_sources': benchmark_python, 'dependency_metadata_sha256': metadata,
        'dependency_site': str(dependency_site), 'compatibility_hook_sha256': digest(HOOK),
        'runner_sha256': digest(RUNNER), 'partial_helper_sha256': digest(PARTIAL_HELPER),
        'cpython_reference': {'prefix': CP_STEM, 'input_sha256': CP_HASHES,
            'started_utc': cp['started_utc'], 'completed_utc': cp['completed_utc'],
            'binary_sha256': cp['sha256_end'], 'attempted_definitions': 97, 'recorded_subtests': 124,
            'comparison': 'Unpaired October 7 saved CPython reference versus fresh XLang3; no CP rerun'},
        'dependency_identity_scope': 'Historical CP metadata hashes match. Current source/data/native bytes are pinned before/after; historical metadata does not retrospectively prove all historical package/data bytes.',
        'raw_stream_policy': 'Binary child stdout+stderr merged by one OS pipe; exact received bytes retained without trimming or newline conversion',
        'idle_guards': [], 'raw': [], 'tracked_sha256_before': tracked,
        'sha256_start': {'exe': digest(CANDIDATE), 'dll': digest(CANDIDATE.with_name('xlang3_runtime.dll'))},
        'scope': 'Attempt all 97 definitions once; retain all worker failures/timeouts/partial files. Failed definitions are never scored.'}

    def save():
        provenance.write_bytes((json.dumps(record, indent=2) + '\n').encode('utf-8'))

    def idle(label):
        command = 'Get-CimInstance Win32_Process | Select-Object Name,ProcessId | ConvertTo-Json -Compress'
        result = subprocess.run(['powershell', '-NoProfile', '-Command', command], capture_output=True, check=True)
        rows = json.loads(result.stdout.decode('utf-8-sig') or '[]')
        if isinstance(rows, dict): rows = [rows]
        compiler = {'cl.exe', 'link.exe', 'ninja.exe', 'msbuild.exe', 'cmake.exe', 'ctest.exe',
                    'nmake.exe', 'lld-link.exe', 'clang-cl.exe'}
        busy = [row for row in rows if row['ProcessId'] != os.getpid() and
            (row['Name'].lower() in compiler or row['Name'].lower().startswith(('python', 'xlang3')))]
        record['idle_guards'].append({'phase': label, 'allowed_controller_pid': os.getpid(), 'busy': busy})
        save()
        assert not busy, busy

    child = None
    code = None
    valid = False
    finish_watch = None
    full_row = {'name': 'all-97', 'stdout_stderr_merged_log': log_path.name, 'passed': False}
    record['raw'].append(full_row)
    try:
        idle('before-full-run')
        # Full current inventories add source/data/native byte coverage for this
        # new run. They do not invent equivalent historical CP coverage.
        record['benchmark_inputs_sha256'] = tree_inputs(BENCHMARK_ROOT)
        record['dependency_inputs_sha256'] = tree_inputs(dependency_site)
        record['git_working_changes_before'] = subprocess.check_output(
            ['git', 'status', '--short', '--untracked-files=no'], cwd=ROOT, text=True)
        env = os.environ.copy()
        assert not env.get('XLANG3_VM_OPCODE_TIMING'), 'Official run must be uninstrumented'
        for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING', 'PYTHONOPTIMIZE'):
            env.pop(name, None)
        env.update(XLANG3_PYTHON_LIB=str(CP.parent / 'Lib'), PYTHONPATH=str(HOOK.parent),
            PYTHONIOENCODING='utf-8', PYTHONPYCACHEPREFIX=str(ROOT / ('scratch/performance/pycache-' + args.prefix)))
        command = [str(CP), str(RUNNER), '--runtime', str(CANDIDATE), '--benchmarks', 'all', '--mode', 'fast',
            '--case-timeout', '300', '--case-timeout-override', 'networkx*=600',
            '--dependency-site', str(dependency_site), '--output', str(output)]
        record.update(status='running', command=command)
        full_row['command'] = command
        idle('immediately-before-full-run')
        assert all(Path(path).is_file() and digest(path) == value for path, value in tracked.items())
        finish_watch = watcher.start_timing_process_watch(args.prefix, 'all-97', full_row)
        with log_path.open('xb') as log:
            child = subprocess.Popen(command, cwd=ROOT, env=env, stdin=subprocess.DEVNULL,
                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
            record['manager_child_pid'] = child.pid
            save()
            print('Full XLang3 all-97 attempt started; saved CP3147 reference reused:', child.pid, flush=True)
            for raw in iter(child.stdout.readline, b''):
                log.write(raw)
                log.flush()
                sys.stdout.write(raw.decode('utf-8', errors='replace'))
                sys.stdout.flush()
            code = child.wait()
        idle('after-full-run')
        text = log_path.read_text(encoding='utf-8', errors='replace')
        sections = summary.case_sections(text)
        headers = [summary.CASE_LINE.match(line).group(1) for line in text.splitlines() if summary.CASE_LINE.match(line)]
        failures, details = summary.failure_details(text)
        record.update(exit_code=code, attempted_definitions=len(sections), failed_definitions=failures,
            failure_details=details, attempted_definition_names=list(sections),
            definitions_missing=sorted(set(definitions) - sections.keys()),
            definitions_unexpected=sorted(sections.keys() - set(definitions)), header_count=len(headers))
        assert len(headers) == 97 and len(sections) == 97 and set(sections) == set(definitions), 'Incomplete all-97 manager attempt'
        assert failures.keys() <= set(definitions)
        if output.is_file():
            try:
                record['recorded_subtests'] = len(summary.benchmark_map(document(output)))
                record['official_json_valid'] = True
            except Exception as error:
                record['official_json_valid'] = False
                record['official_json_error'] = repr(error)
        else:
            record['official_json_valid'] = False
        assert record['official_json_valid'], 'Keep raw incomplete output, but do not score it'
        assert tree_inputs(BENCHMARK_ROOT) == record['benchmark_inputs_sha256'], 'Benchmark source/data bytes changed'
        assert tree_inputs(dependency_site) == record['dependency_inputs_sha256'], 'Dependency source/data/native bytes changed'
        record['status'] = 'finished_with_benchmark_failures' if failures or code else 'finished'
        valid = True
    except BaseException as error:
        record.update(status='incomplete_or_invalid_full_attempt', controller_error=repr(error))
    finally:
        try:
            if child is not None and child.poll() is None:
                subprocess.run(['taskkill', '/F', '/T', '/PID', str(child.pid)], capture_output=True, timeout=10, check=False)
                if child.poll() is None: child.kill()
                child.wait(timeout=10)
            full_row['owned_child_cleanup_completed'] = True
        except BaseException as cleanup_error:
            full_row.update(owned_child_cleanup_completed=False, cleanup_error=repr(cleanup_error))
            record['status'] = 'invalid_owned_child_cleanup'
            valid = False
        finally:
            for key, path in (('merged_log_sha256', log_path), ('output_sha256', output)):
                try: full_row[key] = digest(path) if path.is_file() else None
                except BaseException as hash_error:
                    full_row[key] = None
                    full_row[key + '_error'] = repr(hash_error)
                    record['status'] = 'invalid_raw_output_hash_failure'
                    valid = False
            try:
                full_row['measurement_valid'] = finish_watch() if finish_watch is not None else False
            except BaseException as watch_error:
                full_row.update(measurement_valid=False, watch_finish_error=repr(watch_error))
            if finish_watch is not None and not full_row['measurement_valid']:
                record['status'] = 'invalid_external_process_overlap_or_watch_failure'
                valid = False
            full_row['exit_code'] = code
            full_row['all_97_attempted'] = record.get('attempted_definitions') == 97
            full_row['passed'] = valid
            save()
        after = {path: digest(path) if Path(path).is_file() else None for path in tracked}
        record.update(terminal=True, completed_utc=datetime.now(timezone.utc).isoformat(),
            sha256_end={'exe': after.get(str(CANDIDATE.resolve())),
                'dll': after.get(str(CANDIDATE.with_name('xlang3_runtime.dll').resolve()))},
            tracked_sha256_after=after, hashes_unchanged=after == tracked,
            log_sha256=full_row.get('merged_log_sha256'), output_sha256=full_row.get('output_sha256'),
            timing_measurement_valid=full_row.get('measurement_valid', False),
            candidate_release_tree_unchanged=raw_tree(RELEASE) == binaries,
            baseline_tree_unchanged={p.relative_to(BASELINE).as_posix(): digest(p) for p in sorted(BASELINE.rglob('*')) if p.is_file()} == baseline,
            accepted_release_tree_unchanged=relative_tree(accepted_release) == accepted['files_sha256'],
            accepted_source_tree_unchanged=relative_tree(accepted_sources) == accepted['source_snapshot_sha256'],
            head_unchanged=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip() == actual_head)
        partial = DATA / (args.prefix + '-partial')
        record['partial_evidence_sha256'] = {p.relative_to(DATA).as_posix(): digest(p)
            for p in sorted(partial.rglob('*')) if p.is_file()} if partial.is_dir() else {}
        for name, directory in (('benchmark', BENCHMARK_ROOT), ('dependency', dependency_site)):
            key = name + '_inputs_sha256'
            record[name + '_inputs_unchanged'] = tree_inputs(directory) == record[key] if key in record else None
        if (not record['hashes_unchanged'] or not record['candidate_release_tree_unchanged'] or not record['baseline_tree_unchanged']
                or not record['accepted_release_tree_unchanged'] or not record['accepted_source_tree_unchanged'] or not record['head_unchanged']
                or record['benchmark_inputs_unchanged'] is False or record['dependency_inputs_unchanged'] is False):
            record['status'] = 'invalid_hash_drift'
            valid = False
        full_row['passed'] = valid
        full_row['attempt_capture_valid'] = valid
        full_row['suite_passed'] = valid and code == 0 and not record.get('failed_definitions', {})
        record['terminal_record'] = {'status': record['status'], 'all_97_attempted': record.get('attempted_definitions') == 97,
            'hashes_unchanged': record['hashes_unchanged'], 'benchmark_exit_code': code,
            'failed_definitions_count': len(record.get('failed_definitions', {})),
            'scope': 'Controller completion is not a successful benchmark suite when definitions failed'}
        save()
    print('Terminal full attempt:', record['status'], 'attempted', record.get('attempted_definitions'),
          'failures', len(record.get('failed_definitions', {})), 'benchmark exit', code, flush=True)
    return 0 if valid else 2


if __name__ == '__main__':
    raise SystemExit(main())
