"""One fresh XLang3 all-97 attempt, reusing pinned October 7 CPython 3.14.7.

Prepared only. Root launches after final candidate correctness/gate validation.
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
import os
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


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def document(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def source_map(row):
    if 'source_sha256' in row:
        return row['source_sha256']
    return {name: value['working_sha256'] for name, value in row['files'].items()}


def tree_inputs(directory):
    # Hash existing installed source/data/native bytes, excluding only bytecode.
    # Historical CP provenance pins METADATA, not all these historical bytes.
    return {p.relative_to(directory).as_posix(): digest(p) for p in sorted(directory.rglob('*'))
        if p.is_file() and '__pycache__' not in p.parts and p.suffix not in ('.pyc', '.pyo')}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--validation', type=Path, required=True)
    parser.add_argument('--source-inventory', type=Path, required=True)
    parser.add_argument('--head', required=True, help='Actual final HEAD/ref; resolved and checked, never guessed')
    parser.add_argument('--accepted-base', default='a09f84f5')
    parser.add_argument('--prefix', default='pyperformance-xlang3-full-refresh-fast-20261008')
    parser.add_argument('--canonical-status', type=Path,
        default=DATA / 'pyperformance-xlang3-live-eval-vs-cpython3147-full-fast-20261007-all-97-status.csv')
    args = parser.parse_args()
    assert sys.version_info[:3] == (3, 14, 7) and Path(sys.executable).resolve() == CP.resolve()
    assert re.fullmatch(r'[a-z0-9-]+', args.prefix)
    assert not any(DATA.glob(args.prefix + '*')), 'Preserve earlier evidence; use a fresh prefix'
    output = DATA / (args.prefix + '.json')
    log_path = DATA / (args.prefix + '.log')
    provenance = DATA / (args.prefix + '-provenance.json')
    validation_path, inventory_path = (p.resolve(strict=True) for p in (args.validation, args.source_inventory))
    validation, inventory = document(validation_path), document(inventory_path)
    assert validation.get('terminal') and validation.get('hashes_unchanged')
    assert validation.get('correctness_passed') and validation['status'] == 'validated'
    assert validation['source_inventory_sha256'] == digest(inventory_path)
    sources = source_map(inventory)
    assert sources and sources == validation['source_sha256']
    assert all(re.fullmatch(r'[0-9a-f]{64}', value) for value in sources.values())
    resolved_head = subprocess.check_output(['git', 'rev-parse', '--verify', args.head], cwd=ROOT, text=True).strip()
    actual_head = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    assert actual_head == resolved_head, 'Final HEAD differs from supplied head'
    accepted_base = subprocess.check_output(['git', 'rev-parse', '--verify', args.accepted_base], cwd=ROOT, text=True).strip()
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
    tracked = {}

    def track(path, expected=None):
        path = Path(path).resolve(strict=True)
        value = digest(path)
        if expected is not None: assert value == expected, str(path)
        assert str(path) not in tracked or tracked[str(path)] == value
        tracked[str(path)] = value
        return value

    for path in (validation_path, inventory_path, args.canonical_status, gate_path, Path(__file__),
                 HOOK, RUNNER, SUMMARY, PARTIAL_HELPER, CP, CP.with_name('python314.dll'), *cp_paths.values()):
        track(path)
    for path, value in sources.items(): track(ROOT / path, value)
    for path, value in validation['binaries_sha256'].items(): track(ROOT / path, value)
    assert str(CANDIDATE.resolve()) in tracked and str(CANDIDATE.with_name('xlang3_runtime.dll').resolve()) in tracked
    record = {'status': 'preflight', 'terminal': False, 'pid': os.getpid(),
        'started_utc': datetime.now(timezone.utc).isoformat(), 'source_base_commit': actual_head,
        'accepted_base_commit': accepted_base, 'source_inventory': str(inventory_path),
        'source_inventory_sha256': digest(inventory_path), 'source_sha256': sources,
        'validation': str(validation_path), 'validation_sha256': digest(validation_path),
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
        'idle_guards': [], 'tracked_sha256_before': tracked,
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
        for name in ('PYTHONPATH', 'PYTHONPYCACHEPREFIX', 'PYTHONIOENCODING'):
            env.pop(name, None)
        env.update(XLANG3_PYTHON_LIB=str(CP.parent / 'Lib'), PYTHONPATH=str(HOOK.parent),
            PYTHONIOENCODING='utf-8', PYTHONPYCACHEPREFIX=str(ROOT / ('scratch/performance/pycache-' + args.prefix)))
        command = [str(CP), str(RUNNER), '--runtime', str(CANDIDATE), '--benchmarks', 'all', '--mode', 'fast',
            '--case-timeout', '300', '--case-timeout-override', 'networkx*=600',
            '--dependency-site', str(dependency_site), '--output', str(output)]
        record.update(status='running', command=command)
        with log_path.open('xb') as log:
            child = subprocess.Popen(command, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
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
        if child is not None and child.poll() is None:
            try:
                subprocess.run(['taskkill', '/F', '/T', '/PID', str(child.pid)], capture_output=True, timeout=10, check=False)
                if child.poll() is None: child.kill()
                child.wait(timeout=10)
            except Exception as cleanup_error:
                record['cleanup_error'] = repr(cleanup_error)
    finally:
        after = {path: digest(path) if Path(path).is_file() else None for path in tracked}
        record.update(terminal=True, completed_utc=datetime.now(timezone.utc).isoformat(),
            sha256_end={'exe': after.get(str(CANDIDATE.resolve())),
                'dll': after.get(str(CANDIDATE.with_name('xlang3_runtime.dll').resolve()))},
            tracked_sha256_after=after, hashes_unchanged=after == tracked,
            log_sha256=digest(log_path) if log_path.is_file() else None,
            output_sha256=digest(output) if output.is_file() else None)
        partial = DATA / (args.prefix + '-partial')
        record['partial_evidence_sha256'] = {p.relative_to(DATA).as_posix(): digest(p)
            for p in sorted(partial.rglob('*')) if p.is_file()} if partial.is_dir() else {}
        if not record['hashes_unchanged']:
            record['status'] = 'invalid_hash_drift'
            valid = False
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
