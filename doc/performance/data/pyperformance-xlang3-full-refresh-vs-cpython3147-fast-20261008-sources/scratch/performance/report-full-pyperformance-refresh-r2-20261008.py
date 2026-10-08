"""Prepare full all-97 report/archive only after the new XLang3 receipt is terminal.

No measurement or compiler is invoked. Reuse the existing official-JSON summary
generator and preserve complete failure/partial logs, values and warning evidence.
"""
from __future__ import annotations
import argparse
import csv
from datetime import datetime, timezone
import hashlib
import importlib.util
import io
import json
import math
from pathlib import Path
import re
import statistics
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
GENERATOR = ROOT / 'benchmarks/diagnostics/summarize_pyperformance_comparison.py'


def digest(raw):
    return hashlib.sha256(raw).hexdigest()


def csv_bytes(fields, rows):
    stream = io.StringIO(newline='')
    writer = csv.DictWriter(stream, fieldnames=fields, lineterminator='\n')
    writer.writeheader()
    writer.writerows(rows)
    return stream.getvalue().encode('utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--run-provenance', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, default=ROOT / 'scratch/performance/full-refresh-report-preview-20261008')
    parser.add_argument('--prefix', default='pyperformance-xlang3-full-refresh-vs-cpython3147-fast-20261008')
    parser.add_argument('--canonical-status', type=Path,
        default=DATA / 'pyperformance-xlang3-live-eval-vs-cpython3147-full-fast-20261007-all-97-status.csv')
    parser.add_argument('--gc-evidence', type=Path, default=DATA / 'gc-traversal-coverage-exclusion-20261007.json')
    args = parser.parse_args()
    assert sys.version_info[:3] == (3, 14, 7) and Path(sys.executable).resolve() == CP.resolve()
    assert re.fullmatch(r'[a-z0-9-]+', args.prefix)
    out = args.output_dir.resolve()
    assert not any(out.glob('**/*')) if out.exists() else True, 'Use a fresh empty output directory'
    archive = Path('data') / (args.prefix + '-sources')
    buffered, source_paths = {}, {}

    def keep(path, expected=None):
        path = Path(path).resolve(strict=True)
        raw = path.read_bytes()
        if expected is not None: assert digest(raw) == expected, str(path)
        name = path.relative_to(ROOT).as_posix()
        assert name not in buffered or buffered[name] == raw
        assert out != path and out not in path.parents, 'Destination may not be an input'
        buffered[name] = raw
        source_paths[path] = raw
        return raw

    def read(path):
        return json.loads(keep(path).decode('utf-8-sig'))

    xrecord = read(args.run_provenance)
    assert xrecord.get('terminal') and xrecord.get('hashes_unchanged')
    assert xrecord['status'] in ('finished', 'finished_with_benchmark_failures')
    assert xrecord['expected_definitions'] == xrecord['attempted_definitions'] == xrecord['header_count'] == 97
    assert xrecord['mode'] == 'fast' and xrecord['benchmarks'] == 'all'
    assert xrecord['case_timeout_seconds'] == 300 and xrecord['case_timeout_overrides'] == {'networkx*': 600}
    run_stem = args.run_provenance.name.removesuffix('-provenance.json')
    assert run_stem != args.run_provenance.name
    xjson_path, xlog_path = (DATA / (run_stem + suffix) for suffix in ('.json', '.log'))
    xdata = json.loads(keep(xjson_path, xrecord['output_sha256']))
    # Windows pipes may contain CRCRLF; normalize only the parser view, never raw evidence.
    xlog = keep(xlog_path, xrecord['log_sha256']).decode('utf-8', errors='replace').replace('\r\n', '\n').replace('\r', '\n')
    cp_stem = xrecord['cpython_reference']['prefix']
    cp_paths = {suffix: DATA / (cp_stem + suffix) for suffix in ('.json', '.log', '-provenance.json')}
    for suffix, path in cp_paths.items(): keep(path, xrecord['cpython_reference']['input_sha256'][suffix])
    crecord = read(cp_paths['-provenance.json'])
    cdata = read(cp_paths['.json'])
    clog = cp_paths['.log'].read_text(encoding='utf-8')
    assert crecord['status'] == 'finished' and crecord['runtime_version'] == '3.14.7' and crecord['exit_code'] == 0
    assert crecord['sha256_start'] == crecord['sha256_end'] == xrecord['cpython_reference']['binary_sha256']
    assert crecord['attempted_definitions'] == crecord['expected_definitions'] == 97 and not crecord['failed_definitions']
    assert datetime.fromisoformat(crecord['completed_utc']) < datetime.fromisoformat(xrecord['started_utc'])
    for key in ('compatibility_hook_sha256', 'runner_sha256'):
        assert crecord[key] == xrecord[key]
    for key in ('benchmark_python_sources', 'dependency_metadata_sha256'):
        assert {name.replace('\\', '/'): value for name, value in crecord[key].items()} == xrecord[key]
    spec = importlib.util.spec_from_file_location('refresh_report_summary', GENERATOR)
    summary = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(summary)
    keep(GENERATOR, xrecord['tracked_sha256_before'][str(GENERATOR.resolve())])
    keep(args.canonical_status, xrecord['tracked_sha256_before'][str(args.canonical_status.resolve())])
    for path in (xrecord['validation'], xrecord['source_inventory']):
        keep(path, xrecord['tracked_sha256_before'][str(Path(path).resolve())])
    validation = read(xrecord['validation'])
    assert validation['status'] == 'validated' and validation['terminal'] and validation['hashes_unchanged']
    gate_path = DATA / xrecord['fixed_gate']
    gate = json.loads(keep(gate_path, xrecord['fixed_gate_sha256']))
    assert gate['status'] == 'pass' and len(gate['cases']) == 11
    assert (gate['repeats'], gate['warmup'], gate['threshold']) == (21, 5, .1)
    for path, expected in xrecord['source_sha256'].items(): keep(ROOT / path, expected)
    for path in (Path(__file__), ROOT / 'scratch/performance/run-full-pyperformance-refresh-20261008.py',
                 ROOT / 'benchmarks/diagnostics/run_pyperformance_xlang3_shimmed.py',
                 ROOT / 'benchmarks/diagnostics/preserve_pyperformance_partial.py',
                 ROOT / 'benchmarks/diagnostics/pyperf_compat/sitecustomize.py'):
        keep(path, xrecord['tracked_sha256_before'].get(str(path.resolve())))
    for relative, expected in xrecord['partial_evidence_sha256'].items(): keep(DATA / relative, expected)
    cp_partial = DATA / (cp_stem + '-partial')
    if cp_partial.is_dir():
        for path in sorted(cp_partial.rglob('*')):
            if path.is_file(): keep(path)
    xbench, cbench = summary.benchmark_map(xdata), summary.benchmark_map(cdata)
    failures, _ = summary.failure_details(xlog)
    cp_failures, _ = summary.failure_details(clog)
    assert failures == xrecord['failed_definitions'] and not cp_failures
    with args.canonical_status.open(encoding='utf-8-sig', newline='') as stream:
        canonical = list(csv.DictReader(stream))
    names = [row['benchmark'] for row in canonical]
    assert len(names) == len(set(names)) == 97
    assert set(summary.case_sections(xlog)) == set(summary.case_sections(clog)) == set(names)
    name_to_case = {name: row['benchmark'] for row in canonical for key in ('CPython subtests', 'XLang3 subtests')
        for name in summary.parse_subtests(row.get(key, ''))}
    name_to_case.update({name: name for name in names})
    for log, bench in ((xlog, xbench), (clog, cbench)):
        for case, section in summary.case_sections(log).items():
            for name in summary.RESULT_LINE.findall(section):
                if name in bench:
                    assert name not in name_to_case or name_to_case[name] == case
                    name_to_case[name] = case
    assert (xbench.keys() | cbench.keys()) <= name_to_case.keys()
    exclusions = {}
    historical_gc = None
    if 'gc_traversal' in xbench:
        historical_gc = read(args.gc_evidence)
        exclusions['gc_traversal'] = 'Traversal work remains uncertified; retain raw status/timing and withhold speed score pending independent current workload coverage.'
    exclusion_path = out / 'data' / (args.prefix + '-correctness-exclusions.json')
    exclusions_record = {'scope': 'Conservative continued score withholding; no assertion that changed current sources exactly match historical GC audit',
        'invalid_subtests': exclusions, 'historical_evidence_path': str(args.gc_evidence) if historical_gc else None,
        'historical_evidence_sha256': digest(args.gc_evidence.read_bytes()) if historical_gc else None,
        'current_run_provenance_sha256': digest(args.run_provenance.read_bytes())}
    # All source/receipt/partial bytes are buffered before output writes; never
    # scan archive destinations or previous report archives.
    assert all(path.read_bytes() == raw for path, raw in source_paths.items()), 'Input drift during export preflight'
    (out / 'data').mkdir(parents=True)
    exclusion_path.write_bytes((json.dumps(exclusions_record, indent=2) + '\n').encode('utf-8'))
    # The existing generator links inputs relative to its output directory.
    # Give it byte-exact local copies, preserving the original selected inputs
    # separately in the explicit archive and avoiding remote-path rel() errors.
    local_inputs = {}
    for label, path in (('xlang.json', xjson_path), ('xlang.log', xlog_path),
                        ('cpython3147.json', cp_paths['.json']), ('cpython3147.log', cp_paths['.log'])):
        local = out / 'data' / (args.prefix + '-input-' + label)
        local.write_bytes(source_paths[path.resolve()])
        local_inputs[label] = local
    command = [str(CP), str(GENERATOR), '--xlang-json', str(local_inputs['xlang.json']), '--cpython-json', str(local_inputs['cpython3147.json']),
        '--xlang-log', str(local_inputs['xlang.log']), '--cpython-log', str(local_inputs['cpython3147.log']), '--canonical-status', str(args.canonical_status),
        '--output-dir', str(out / 'data'), '--prefix', args.prefix,
        '--release-exe-sha256', xrecord['sha256_end']['exe'], '--release-dll-sha256', xrecord['sha256_end']['dll'],
        '--case-timeout', '300', '--case-timeout-override', 'networkx*=600s', '--xlang-stdlib', 'C:/Python/Python314/Lib']
    for name, reason in exclusions.items(): command += ['--invalid-subtest', name + '=' + reason]
    if exclusions: command += ['--correctness-evidence', str(exclusion_path)]
    generated_log = out / 'data' / (args.prefix + '-generator.log')
    with generated_log.open('xb') as log:
        result = subprocess.run(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    assert result.returncode == 0, 'Preserve failed report-generator output; no accepted report'
    status_path = out / 'data' / (args.prefix + '-all-97-status.csv')
    with status_path.open(encoding='utf-8-sig', newline='') as stream: statuses = list(csv.DictReader(stream))
    assert len(statuses) == 97 and {row['benchmark'] for row in statuses} == set(names)
    assert all(row['CPython 3.14 status'] == 'completed' for row in statuses)
    with (out / 'data' / (args.prefix + '-subtests.csv')).open(encoding='utf-8-sig', newline='') as stream:
        scored = [row for row in csv.DictReader(stream) if row['CPython / XLang3 speedup']]
    assert not any(row['benchmark'] in failures or row['subtest'] in exclusions for row in scored)
    samples, variation = [], []
    warnings = {}
    for runtime, bench, log, failed in (('xlang3', xbench, xlog, failures), ('cpython3147_saved', cbench, clog, cp_failures)):
        sections = summary.case_sections(log)
        warnings[runtime] = sorted(case for case, text in sections.items() if 'WARNING: the benchmark result may be unstable' in text)
        for name, row in bench.items():
            case = name_to_case[name]
            score_state = 'failed_definition_unscored' if case in failed else 'workload_unscored' if name in exclusions else 'completed_raw'
            values = summary.values(row)
            assert all(math.isfinite(value) and value > 0 for value in values)
            for run_index, run in enumerate(row['runs']):
                for sample_index, value in enumerate(run.get('values', [])):
                    samples.append({'runtime': runtime, 'benchmark': case, 'subtest': name, 'run': run_index,
                        'sample': sample_index, 'seconds': value, 'scope': score_state})
            if values:
                mean = statistics.fmean(values)
                sd = statistics.stdev(values) if len(values) > 1 else None
                variation.append({'runtime': runtime, 'benchmark': case, 'subtest': name, 'scope': score_state,
                    'samples': len(values), 'mean_seconds': mean, 'sd_seconds': sd,
                    'cv_percent': sd / mean * 100 if sd is not None else None,
                    'min_seconds': min(values), 'max_seconds': max(values), 'definition_warning': case in warnings[runtime]})
    partial_rows = []
    for relative, expected in xrecord['partial_evidence_sha256'].items():
        path = DATA / relative
        if not path.name.endswith('.provenance.json'): continue
        item = json.loads(keep(path, expected))
        raw_path = path.parent / item['partial_output']
        raw = keep(raw_path, item['sha256'])
        assert item['definition_status'] == 'failed' and item['definition'] in failures
        base = {'runtime': 'xlang3', 'benchmark': item['definition'], 'partial_file': raw_path.relative_to(DATA).as_posix(),
            'sha256': item['sha256'], 'json_valid': item['json_valid'], 'subtest': '', 'sample_count': 0,
            'mean_seconds': '', 'sd_seconds': '', 'not_scored_reason': 'Failed definition; separate snapshot, no ratios/pooling'}
        observed = False
        if item['json_valid'] and not item.get('schema_error'):
            try:
                for name, row in summary.benchmark_map(json.loads(raw)).items():
                    values = summary.values(row)
                    if values:
                        observed = True
                        partial_rows.append({**base, 'subtest': name, 'sample_count': len(values),
                            'mean_seconds': statistics.fmean(values), 'sd_seconds': statistics.stdev(values) if len(values) > 1 else ''})
            except (ValueError, TypeError, AttributeError):
                pass
        if not observed: partial_rows.append(base)
    extra = {args.prefix + '-all-raw-values.csv': csv_bytes(
        ['runtime', 'benchmark', 'subtest', 'run', 'sample', 'seconds', 'scope'], samples),
        args.prefix + '-sample-variation.csv': csv_bytes(['runtime', 'benchmark', 'subtest', 'scope', 'samples',
            'mean_seconds', 'sd_seconds', 'cv_percent', 'min_seconds', 'max_seconds', 'definition_warning'], variation),
        args.prefix + '-failed-partial-subtests.csv': csv_bytes(['runtime', 'benchmark', 'partial_file', 'sha256',
            'json_valid', 'subtest', 'sample_count', 'mean_seconds', 'sd_seconds', 'not_scored_reason'], partial_rows)}
    for name, raw in extra.items(): (out / 'data' / name).write_bytes(raw)
    report_path = out / (args.prefix + '.md')
    text = report_path.read_text(encoding='utf-8')
    text = text.replace('# XLang3 vs CPython 3.14.7: corrected full pyperformance run',
        '# Fresh XLang3 all-97 attempt versus saved CPython 3.14.7', 1)
    text = text.replace('\n\n', '\n\nThe CPython reference was measured on October 7, 2026; '
        'XLang3 is freshly measured. This is an **unpaired saved-reference comparison**. '
        'CPython EXE/DLL, benchmark Python sources, hook and dependency metadata are pinned, '
        'but historical metadata alone does not prove every historical package/data byte.\n\n', 1)
    text += ('\n## Saved reference dates and input identity\n\n'
        f"CPython completed all 97 definitions / 124 subtests on {crecord['started_utc']} through {crecord['completed_utc']}. "
        f"The fresh XLang3 run began {xrecord['started_utc']} and ended {xrecord['completed_utc']}. "
        'These are **unpaired saved-reference comparisons**, not paired optimization gains. CPython throughput is 1x; '
        'the chart uses CP time / XLang3 time, with values over 1x favoring XLang3. Fast-mode variation/warnings remain.\n\n'
        'Exact CPython EXE/DLL, manager versions, compatibility hook, all 224 recorded benchmark Python sources and '
        '82 dependency METADATA identities match the saved October 7 reference. Current benchmark/dependency source, '
        'data and native bytes were additionally pinned before/after the fresh run. **Historical package METADATA '
        'does not prove every dependency source/data byte was unchanged since October 7.** The existing 43-source SQL '
        'audit covers that SQL scope only; no broader retrospective package-source equivalence is asserted.\n\n'
        f"[All raw values](data/{args.prefix}-all-raw-values.csv) and [sample variation](data/{args.prefix}-sample-variation.csv) "
        'retain every full-JSON timing, including clearly marked unscored failed definitions/workloads. '
        f"[Failed partial snapshots](data/{args.prefix}-failed-partial-subtests.csv) remain separate and unscored; "
        'invalid/truncated files retain index rows and exact raw bytes. No values/outliers are trimmed or pooled.\n\n'
        'Worker completion and nominal wins do not certify equal work in every benchmark. GC traversal remains '
        'conservatively unscored pending current independent workload coverage. ElementTree variants retain their '
        'original recorded names and backend metadata in raw JSON; accelerator/pure-Python variants are not renamed.\n')
    report_path.write_text(text, encoding='utf-8', newline='\n')
    # Preserve every explicitly selected raw file after generator success, then
    # freeze the output manifest. No recursion through past archives is used.
    assert all(path.read_bytes() == raw for path, raw in source_paths.items()), 'Input drift during static report generation'
    for relative, raw in buffered.items():
        target = out / archive / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open('xb') as stream: stream.write(raw)
    generated = [p for p in sorted(out.rglob('*')) if p.is_file()]
    manifest = {'status': 'terminal_all_97_evidence_report', 'source_head': xrecord['source_base_commit'],
        'run_provenance_sha256': digest(args.run_provenance.read_bytes()), 'generator_command': command,
        'report_workflow_sha256': digest(Path(__file__).read_bytes()), 'warning_definitions': warnings,
        'scored_subtests': len(scored), 'failed_definitions': len(failures), 'all_97_attempted': True,
        'reference_scope': xrecord['dependency_identity_scope'],
        'files': [{'path': p.relative_to(out).as_posix(), 'sha256': digest(p.read_bytes()), 'bytes': p.stat().st_size} for p in generated]}
    (out / 'data' / (args.prefix + '-export-manifest.json')).write_bytes((json.dumps(manifest, indent=2) + '\n').encode('utf-8'))
    print('Static full report exported; 97 outcomes, failures', len(failures), 'scored subtests', len(scored),
          'raw files', len(buffered), '; no measurement launched')


if __name__ == '__main__':
    main()
