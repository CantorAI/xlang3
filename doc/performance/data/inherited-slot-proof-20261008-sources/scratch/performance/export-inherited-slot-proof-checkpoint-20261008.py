"""Export saved inherited-slot proof evidence only; never run a runtime, build or git.

Run only after the relevant controllers are terminal, with CPython 3.14.7.
Default output is an isolated scratch preview. --output-dir doc/performance
creates the normal checkpoint layout when the parent decides to publish it.
Every raw input and original log/source byte is copied without normalization.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import html
import io
import json
import math
import os
from pathlib import Path
import re
import statistics
import sys

ROOT = Path(r'D:\CantorAI\xlang3')
DATA = ROOT / 'doc/performance/data'
PREFIX = 'inherited-slot-proof-checkpoint-20261008'
BENCHMARK = 'sqlglot_v2_parse'
OFFICIAL_PREFIXES = {
    'cpython3147': 'pyperformance-cpython3147-live-eval-full-fast-20261007',
    'historical_xlang3': 'pyperformance-xlang3-live-eval-full-fast-20261007',
}
EXPECTED_SOURCE = 'd97aaba24f8c49f2afe5c5c740c568be6e89dde3364d5a964a4b72e5a4768a9b'


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def write_exact(path, raw):
    if isinstance(raw, str):
        raw = raw.encode('utf-8')
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        require(path.read_bytes() == raw, 'Refusing to overwrite evidence: ' + str(path))
    else:
        with path.open('xb') as stream:
            stream.write(raw)


def csv_bytes(fields, rows):
    stream = io.StringIO(newline='')
    writer = csv.DictWriter(stream, fieldnames=fields)
    writer.writeheader()
    writer.writerows(rows)
    return stream.getvalue().encode('utf-8')


def checked_seconds(values):
    require(bool(values), 'Missing timing samples')
    require(all(isinstance(v, (int, float)) and math.isfinite(v) and v > 0 for v in values),
            'Timing samples must be finite positive seconds')
    return values


def identity(row):
    return {key: value for key, value in row.items() if key != 'samples_seconds'}


def checkpoint_hashes(record):
    return record.get('hashes_before', record)


def case_label(row):
    return row.get('path', row.get('benchmark', BENCHMARK))


def diagnostic_rows(stage, record):
    require(record['status'] == 'terminal', stage + ' paired controller must be terminal')
    require(record.get('terminal_record') and record.get('hashes_unchanged') and
            record['hashes_before'] == record['hashes_after'], 'Candidate hashes changed or terminal receipt absent')
    samples, rows = [], []
    require(len(record['probes']) == 2, 'Keep both unchanged diagnostic probes')
    for probe in record['probes']:
        require(len(probe['pairs']) == 7, 'Keep all seven process pairs')
        reference = probe['cpython3147']['rows']
        require(len(reference) == (3 if probe['name'] == 'slot' else 1), 'Diagnostic row count changed')
        for index, cp in enumerate(reference):
            cp_values = checked_seconds(cp['samples_seconds'])
            require(len(cp_values) == 5, 'Keep all five CPython reference samples')
            checked = identity(cp)
            if probe['name'] == 'slot':
                require(cp['operations'] == 16384 and cp['checksum'] == 114688, 'Slot work changed')
            else:
                require(cp['parses_per_sample'] == 10, 'Original SQLGlot diagnostic work changed')
            label = case_label(checked)
            for number, seconds in enumerate(cp_values, 1):
                samples.append({'stage': stage, 'probe': probe['name'], 'case': label,
                    'runtime': 'cpython3147', 'pair': '', 'process_order': '',
                    'sample': number, 'seconds': seconds, 'identity_json': json.dumps(checked, sort_keys=True)})
            ratios, runtime_medians = [], {'control': [], 'candidate': []}
            for pair_number, pair in enumerate(probe['pairs'], 1):
                require(pair['order'] == (['control', 'candidate'] if pair_number % 2 else ['candidate', 'control']),
                        'Alternating order changed')
                medians = {}
                for process_order, runtime in enumerate(pair['order'], 1):
                    observed = pair['runs'][runtime]['rows']
                    require(len(observed) == len(reference), 'A neutral/slow diagnostic row was dropped')
                    row = observed[index]
                    require(identity(row) == checked, 'Runtime work differs from CPython reference')
                    values = checked_seconds(row['samples_seconds'])
                    require(len(values) == 5, 'A diagnostic timing sample was dropped')
                    medians[runtime] = statistics.median(values)
                    runtime_medians[runtime].append(medians[runtime])
                    for number, seconds in enumerate(values, 1):
                        samples.append({'stage': stage, 'probe': probe['name'], 'case': label,
                            'runtime': runtime, 'pair': pair_number, 'process_order': process_order,
                            'sample': number, 'seconds': seconds, 'identity_json': json.dumps(checked, sort_keys=True)})
                ratios.append(medians['control'] / medians['candidate'])
            summary = probe['summary'][index]
            require(summary['identity'] == checked and summary['control_time_over_candidate_time'] == ratios,
                    'Saved paired ratio does not match raw samples')
            speed = statistics.median(ratios)
            favorable = sum(value > 1 for value in ratios)
            require(summary['median_speedup'] == speed and summary['pairs_favoring_candidate'] == favorable,
                    'Saved paired summary does not match raw samples')
            cp_median = statistics.median(cp_values)
            control_median = statistics.median(runtime_medians['control'])
            candidate_median = statistics.median(runtime_medians['candidate'])
            rows.append({'stage': stage, 'probe': probe['name'], 'case': label,
                'cpython_reference_median_seconds': cp_median, 'control_process_median_seconds': control_median,
                'candidate_process_median_seconds': candidate_median,
                'paired_control_over_candidate_speed': speed, 'pairs_favoring_candidate': favorable,
                'pair_count': 7, 'descriptive_cpython_over_control_speed': cp_median / control_median,
                'descriptive_cpython_over_candidate_speed': cp_median / candidate_median,
                'paired_ratios_json': json.dumps(ratios), 'identity_json': json.dumps(checked, sort_keys=True),
                'acceptance': 'pending full validation'})
    require(len(rows) == 4 and len(samples) == 300, 'Keep four rows and all 300 samples per revision')
    return samples, rows


def benchmark_values(document):
    found = []
    for item in document.get('benchmarks', []):
        meta = {**document.get('metadata', {}), **item.get('metadata', {})}
        if meta.get('name') == BENCHMARK:
            require(meta.get('unit', 'second') == 'second', 'Official unit must be seconds')
            found.append(item)
    require(len(found) == 1, 'Original official benchmark must appear exactly once')
    # pyperf values already include loops/inner_loops normalization. Warmup and
    # calibration tuples remain in the copied original JSON, never in scores.
    values = [value for run in found[0]['runs'] for value in run.get('values', [])]
    require(len(values) >= 2, 'No completed official measurement samples')
    return checked_seconds(values)


def sqlglot_section(log):
    current, output = None, []
    for line in log.splitlines():
        match = re.match(r'^\s*\[\s*\d+/\d+\]\s+(.+?)\.\.\.\s*$', line)
        if match:
            current = match.group(1)
        elif current == BENCHMARK:
            output.append(line)
    require(bool(output), 'Official original SQLGlot log section missing')
    return '\n'.join(output)


def official_row(runtime, path, log, samples):
    return {'runtime': runtime, 'scope': 'original pyperformance fast mode',
        'source_json': path.relative_to(ROOT).as_posix(), 'json_sha256': sha(path.read_bytes()),
        'sample_count': len(samples), 'mean_seconds': statistics.fmean(samples),
        'sample_standard_deviation_seconds': statistics.stdev(samples),
        'coefficient_of_variation': statistics.stdev(samples) / statistics.fmean(samples),
        'minimum_seconds': min(samples), 'maximum_seconds': max(samples),
        'pyperf_instability_warning': 'WARNING: the benchmark result may be unstable' in sqlglot_section(log),
        'values_seconds': samples}


def chart(title, subtitle, bars, filename, out):
    width, left, right, top, stride = 1140, 315, 100, 98, 36
    plot = width - left - right
    height = top + stride * len(bars) + 60
    maximum = max([1.0, *[row['speed'] for row in bars]]) * 1.08
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<style>text{font-family:Arial,sans-serif;fill:#223047} .title{font-size:20px;font-weight:bold} .small{font-size:13px}</style>',
        '<rect width="100%" height="100%" fill="white"/>',
        f'<text x="24" y="30" class="title">{html.escape(title)}</text>',
        f'<text x="24" y="55" class="small">{html.escape(subtitle)}</text>']
    baseline = left + plot / maximum
    parts.append(f'<line x1="{baseline:.2f}" x2="{baseline:.2f}" y1="78" y2="{height - 44}" stroke="#64748b" stroke-dasharray="5 4"/>')
    parts.append(f'<text x="{baseline:.2f}" y="76" class="small" text-anchor="middle">1× reference</text>')
    for index, row in enumerate(bars):
        y = top + index * stride
        extent = plot * row['speed'] / maximum
        parts.append(f'<text x="{left - 12}" y="{y + 16}" text-anchor="end" class="small">{html.escape(row["label"])}</text>')
        parts.append(f'<rect x="{left}" y="{y}" width="{extent:.2f}" height="23" rx="2" fill="{row["color"]}"/>')
        parts.append(f'<text x="{left + extent + 8:.2f}" y="{y + 16}" class="small">{row["speed"]:.3f}×</text>')
    parts.append(f'<text x="24" y="{height - 16}" class="small">Longer bars mean faster. Above 1× is faster than the named reference; below 1× is slower.</text></svg>')
    write_exact(out / filename, '\n'.join(parts) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path,
                        default=ROOT / 'scratch/performance/inherited-slot-proof-report-preview-20261008')
    args = parser.parse_args()
    require(sys.version_info[:3] == (3, 14, 7) and os.path.normcase(str(Path(sys.executable).resolve())) ==
            os.path.normcase(str(Path(r'C:\Python\Python314\python.exe').resolve())), 'Use exact CPython 3.14.7')
    require(Path.cwd().resolve() == ROOT.resolve(), 'Keep the original run directory')
    out = args.output_dir.resolve()
    require(out == (ROOT / 'doc/performance').resolve() or out.is_relative_to((ROOT / 'scratch/performance').resolve()),
            'Output must be performance documentation or an isolated scratch preview')
    archive = out / 'data/inherited-slot-proof-20261008-sources'
    generated_data = {'inherited-slot-proof-paired-samples-20261008.csv',
                      'inherited-slot-proof-paired-summary-20261008.csv',
                      'inherited-slot-proof-official-samples-20261008.csv',
                      'inherited-slot-proof-official-summary-20261008.csv',
                      'inherited-slot-proof-comparison-20261008.json'}
    early_path = DATA / 'inherited-slot-proof-early-20261008.json'
    paired_path = DATA / 'inherited-slot-proof-paired-20261008.json'
    validation_path = DATA / 'inherited-slot-proof-validation-20261008.json'
    early, paired = read_json(early_path), read_json(paired_path)
    validation = read_json(validation_path) if validation_path.exists() else None
    require(validation is None or validation['status'] != 'running', 'Full validation is still running')
    require(early['status'] == 'terminal' and early.get('terminal_record') and early.get('hashes_unchanged'),
            'Focused correctness must be terminal with unchanged hashes')
    require(early['hashes_before'] == early['hashes_after'] == paired['hashes_before'],
            'Focused correctness and paired inputs differ')
    samples, rows = diagnostic_rows('inherited_R3', paired)
    inputs, payloads = {}, {}

    def keep(path, expected=None, archive_name=None):
        raw = path.read_bytes()
        digest = sha(raw)
        require(expected is None or digest == expected, 'Input hash mismatch: ' + str(path))
        name = archive_name or path.relative_to(ROOT).as_posix()
        require(not Path(name).is_absolute() and '..' not in Path(name).parts, 'Unsafe archive path')
        if name in inputs:
            require(inputs[name]['sha256'] == digest, 'Moving input: ' + str(path))
        inputs[name] = {'source': str(path), 'sha256': digest, 'raw_bytes': len(raw)}
        payloads[name] = raw

    for path in (early_path, paired_path):
        keep(path)
    for record in (early, paired):
        for phase in record['phases']:
            require(phase['status'] == 'passed' and phase['exit_code'] == 0, 'Successful evidence cannot contain failed/running phases')
            for stream in ('stdout', 'stderr'):
                keep(DATA / phase[stream + '_log'], phase[stream + '_sha256'])
    require(all(guard['status'] == 'idle' for record in (early, paired) for guard in record['idle_guards']),
            'Paired timings require idle guards to pass')
    hashes = paired['hashes_before']
    require(len(hashes['source_sha256']) == 19, 'Keep all 19 tested source/probe/controller hashes')
    for name, digest in hashes['source_sha256'].items():
        keep(ROOT / name, digest, 'r3-tested-inputs/' + name)
    lf_prov_path = ROOT / 'scratch/performance/inherited-slot-proof-git-lf-20261008-provenance.json'
    lf_prov = read_json(lf_prov_path)
    require(lf_prov['target_count'] == len(lf_prov['targets']) == 11 and
            lf_prov['paired_evidence_sha256'] == sha(paired_path.read_bytes()), 'LF provenance used different paired inputs')
    keep(lf_prov_path)
    for entry in lf_prov['targets']:
        require(entry['working_sha256'] == hashes['source_sha256'][entry['path']] and
                entry['git_clean_blob_sha1'] == entry['git_lf_blob_sha1'], 'Git LF source identity differs')
        raw = (ROOT / entry['path']).read_bytes()
        normalized = (ROOT / entry['git_lf_copy']).read_bytes()
        require(normalized == raw.replace(b'\r\n', b'\n') and len(raw) == entry['working_bytes'] and
                len(normalized) == entry['git_lf_bytes'], 'Separate LF bytes differ from compiled working bytes')
        keep(ROOT / entry['git_lf_copy'], entry['git_lf_sha256'], 'git-normalized-lf/' + entry['path'])
    candidate_release = ROOT / 'build-repro/main-verify-20261006/Release'
    require({key: sha((candidate_release / name).read_bytes()) for key, name in
             (('exe', 'xlang3.exe'), ('dll', 'xlang3_runtime.dll'))} == hashes['binaries_sha256']['candidate'],
            'Current compiled candidate binary/library changed')
    require(sha(Path(sys.executable).read_bytes()) == hashes['cpython3147_binary_sha256'], 'Current CPython executable changed')
    original_source = Path(r'C:\Python\Python314\Lib\site-packages\pyperformance\data-files\benchmarks\bm_sqlglot_v2\run_benchmark.py')
    require(hashes['benchmark_source_sha256'] == EXPECTED_SOURCE, 'Original SQLGlot source changed')
    keep(original_source, EXPECTED_SOURCE, 'external/python314/bm_sqlglot_v2/run_benchmark.py')
    keep(ROOT / 'benchmarks/diagnostics/pyperf_compat/sitecustomize.py', hashes['compatibility_hook_sha256'])
    keep(ROOT / 'benchmarks/diagnostics/run_pyperformance_xlang3_shimmed.py')
    keep(ROOT / 'benchmarks/check_regression.py')
    control = ROOT / 'build-repro/controls/canonical-slot-checkpoint-20261008'
    control_manifest = control / 'preserved-release-provenance.json'
    preserved = read_json(control_manifest)
    require(preserved['accepted'] is True and preserved['commit'] == 'e2a752f62227ef1088236c68d6a08e480323e112',
            'Control must be the accepted R4 checkpoint')
    require(preserved['files_sha256'] == hashes['preserved_control_release']['files_sha256'] and
            len(preserved['files_sha256']) == 140, 'Accepted control inventory differs')
    require(all(sha((control / name).read_bytes()) == digest for name, digest in preserved['files_sha256'].items()),
            'A preserved accepted control binary/library changed')
    require(hashes['binaries_sha256']['control'] ==
            {'exe': preserved['files_sha256']['xlang3.exe'], 'dll': preserved['files_sha256']['xlang3_runtime.dll']},
            'Accepted control executable identity differs')
    keep(control_manifest, hashes['preserved_control_release']['manifest_sha256'])

    cp_prefix = 'pyperformance-cpython3147-live-eval-full-fast-20261007'
    cp_path, cp_prov_path, cp_log_path = (DATA / (cp_prefix + suffix) for suffix in ('.json', '-provenance.json', '.log'))
    cp_prov = read_json(cp_prov_path)
    require(cp_prov['status'] in ('finished', 'finished_with_benchmark_failures') and
            cp_prov['sha256_start'] == cp_prov['sha256_end'] and cp_prov['runtime_version'] == '3.14.7' and
            cp_prov['mode'] == 'fast' and BENCHMARK not in cp_prov['failed_definitions'], 'CPython reference is incomplete/wrong version')
    require(os.path.normcase(str(Path(cp_prov['runtime_executable']).resolve())) ==
            os.path.normcase(str(Path(r'C:\Python\Python314\python.exe').resolve())) and
            cp_prov['sha256_end']['exe'] == hashes['cpython3147_binary_sha256'], 'CPython executable changed')
    source_hashes = {name.replace('\\', '/'): digest for name, digest in cp_prov['benchmark_python_sources'].items()}
    require(source_hashes['bm_sqlglot_v2/run_benchmark.py'] == EXPECTED_SOURCE and
            cp_prov['compatibility_hook_sha256'] == hashes['compatibility_hook_sha256'], 'CPython benchmark/hook source changed')
    official = [official_row('cpython3147_saved_full_fast', cp_path, cp_log_path.read_text(encoding='utf-8'),
                             benchmark_values(read_json(cp_path)))]
    for path in (cp_path, cp_prov_path, cp_log_path):
        keep(path)

    r4_validation_path = DATA / 'canonical-slot-r4-validation-20261008.json'
    r4 = read_json(r4_validation_path)
    require(r4['status'] == 'validated' and r4['candidate_binary_sha256'] == hashes['binaries_sha256']['control'],
            'Saved R4 official reference must identify the accepted paired control')
    r4_official = r4['official_sqlglot_parse']
    r4_phase = next(phase for phase in r4['phases'] if phase['name'] == 'official-sqlglot-parse')
    r4_path, r4_log_path = DATA / r4_official['output'], DATA / r4_phase['log']
    require(r4_official['exit_code'] == r4_phase['exit_code'] == 0 and
            r4['compatibility_hook_sha256'] == hashes['compatibility_hook_sha256'], 'R4 official phase/hook differs')
    keep(r4_path, r4_official['sha256'])
    keep(r4_log_path, r4_phase['sha256'])
    keep(r4_validation_path)
    official.append(official_row('accepted_r4_saved_fast', r4_path, r4_log_path.read_text(encoding='utf-8'),
                                 benchmark_values(read_json(r4_path))))
    # R4 live source hashes are historical. Read its already committed snapshots;
    # never compare old hashes to the current changed inherited candidate files.
    old_archive = DATA / 'canonical-slot-20261008-sources'
    keep(old_archive / 'manifest.json')
    for name, digest in r4['source_sha256'].items():
        keep(old_archive / 'r4-tested-inputs' / name, digest, 'accepted-r4-tested-inputs/' + name)

    status = 'not measured: full validation absent'
    validation_text = 'Full correctness, fixed gate and official candidate scores are not recorded.'
    if validation is not None:
        status = validation['status']
        require(validation['source_sha256'] == hashes['source_sha256'] and
                validation['candidate_binary_sha256'] == hashes['binaries_sha256']['candidate'],
                'Full validation candidate/source identity differs from focused trial')
        require(validation['early_evidence']['sha256'] == sha(early_path.read_bytes()) and
                validation['paired_evidence']['sha256'] == sha(paired_path.read_bytes()), 'Full validation used different early/paired evidence')
        keep(validation_path)
        for phase in validation['phases']:
            keep(DATA / phase['log'], phase['sha256'])
        counts = validation.get('fixture_counts', {})
        cpp = next((phase for phase in validation['phases'] if phase['name'] == 'cpp'), None)
        cpp_count = None
        if cpp is not None:
            matched = re.search(r'100% tests passed, 0 tests failed out of (\d+)',
                                (DATA / cpp['log']).read_text(encoding='utf-8', errors='replace'))
            cpp_count = int(matched.group(1)) if matched else None
        validation_text = (f'Full validation records {counts.get("core", "unavailable")} core fixtures, '
                           f'{counts.get("compatibility_sections", "unavailable")} compatibility sections, '
                           f'{counts.get("expected_failures", "unavailable")} expected-failure checks and '
                           f'{cpp_count if cpp_count is not None else "unavailable"} C++/SDK/graph tests.')
        if 'fixed_gate' in validation:
            gate_path = DATA / validation['fixed_gate']['output']
            keep(gate_path, validation['fixed_gate']['sha256'])
            gate = read_json(gate_path)
            require(len(gate['cases']) == 11 and (gate['repeats'], gate['warmup'], gate['threshold']) == (21, 5, .1),
                    'Fixed regression gate workload/threshold changed')
            if status == 'validated':
                require(gate['status'] == 'pass' and validation['fixed_gate']['exit_code'] == 0, 'Validated fixed gate must pass')
            validation_text += ' Fixed gate: ' + gate['status'] + ' (11 cases, 21 pairs, five warmups, unchanged 10% tolerance).'
        if status == 'validated':
            require(cpp is not None and cpp['exit_code'] == 0 and cpp_count == 8 and
                    'fixed_gate' in validation and all(phase['exit_code'] == 0 for phase in validation['phases']),
                    'Validated checkpoint must retain complete passing correctness/SDK/graph/gate phases')
            measured = validation['official_sqlglot_parse']
            phase = next(phase for phase in validation['phases'] if phase['name'] == 'official-sqlglot-parse')
            command = phase['command']
            require(measured['exit_code'] == phase['exit_code'] == 0 and measured['sha256'] and
                    '--benchmarks' in command and command[command.index('--benchmarks') + 1] == BENCHMARK and
                    '--mode' in command and command[command.index('--mode') + 1] == 'fast' and
                    any(str(part).endswith('run_pyperformance_xlang3_shimmed.py') for part in command),
                    'Official candidate requires original pyperformance fast command')
            path, log_path = DATA / measured['output'], DATA / phase['log']
            keep(path, measured['sha256'])
            official.append(official_row('inherited_r3_candidate', path, log_path.read_text(encoding='utf-8'),
                                         benchmark_values(read_json(path))))
    cp_mean, accepted_mean = official[0]['mean_seconds'], official[1]['mean_seconds']
    for row in official:
        row['cpython_time_over_runtime_time_speed'] = cp_mean / row['mean_seconds']
        row['runtime_time_over_cpython_time'] = row['mean_seconds'] / cp_mean
        row['accepted_r4_time_over_runtime_time_speed'] = accepted_mean / row['mean_seconds']
    for row in rows:
        row['acceptance'] = status + '; all neutral/slower controls retained'

    def excluded(path):
        resolved = path.resolve()
        return (resolved == archive.resolve() or resolved.is_relative_to(archive.resolve()) or
                (out != (ROOT / 'doc/performance').resolve() and
                 (resolved == out or resolved.is_relative_to(out))) or path.name in generated_data)

    # Freeze the inventory before any output write. Exclude destination trees
    # explicitly, including idempotent publication after a scratch preview.
    for pattern in ('inherited-slot*', 'build-inherited-slot*', 'release-inherited-slot*', 'pyperformance-inherited-slot*'):
        for source in sorted(DATA.glob(pattern)):
            if excluded(source):
                continue
            if source.is_file():
                keep(source)
            elif source.is_dir():
                for path in sorted(source.rglob('*')):
                    if path.is_file() and not excluded(path):
                        keep(path)
    for pattern in ('inherited-slot*', 'inherited_slot*', '*inherited-slot*20261008.py', '*inherited-slot*20261008.ps1'):
        for source in sorted((ROOT / 'scratch/performance').glob(pattern)):
            if excluded(source):
                continue
            if source.is_file():
                keep(source)
            elif source.is_dir() and source.name.endswith(('-sources', '-candidates')):
                for path in sorted(source.rglob('*')):
                    if path.is_file() and not excluded(path):
                        keep(path)
    keep(Path(__file__).resolve())
    require(any('inherited-slot-proof-proposal-r2-20261008.patch' in name for name in inputs) and
            any('inherited-slot-proof-r3-incremental-20261008.patch' in name for name in inputs),
            'Preserve both built unguarded R2 proposal and R3 correction')
    for entry in inputs.values():
        require(sha(Path(entry['source']).read_bytes()) == entry['sha256'], 'Input moved during collection')
    for name, raw in payloads.items():
        write_exact(archive / name, raw)

    write_exact(out / 'data/inherited-slot-proof-paired-samples-20261008.csv', csv_bytes(
        ['stage', 'probe', 'case', 'runtime', 'pair', 'process_order', 'sample', 'seconds', 'identity_json'], samples))
    write_exact(out / 'data/inherited-slot-proof-paired-summary-20261008.csv', csv_bytes(list(rows[0]), rows))
    official_samples = [{'runtime': row['runtime'], 'subtest': BENCHMARK, 'sample': index,
                         'seconds': seconds, 'source_json': row['source_json']}
                        for row in official for index, seconds in enumerate(row['values_seconds'], 1)]
    write_exact(out / 'data/inherited-slot-proof-official-samples-20261008.csv', csv_bytes(
        ['runtime', 'subtest', 'sample', 'seconds', 'source_json'], official_samples))
    official_summary = [{key: value for key, value in row.items() if key != 'values_seconds'} for row in official]
    write_exact(out / 'data/inherited-slot-proof-official-summary-20261008.csv', csv_bytes(list(official_summary[0]), official_summary))
    comparison = {'status': 'terminal evidence export', 'candidate_validation_status': status,
        'scope': 'Inherited slot diagnostic and original official SQLGlot only; no whole-suite claim',
        'speed_convention': 'reference seconds / runtime seconds; above 1x faster',
        'diagnostic_samples': len(samples), 'diagnostic_summary_rows': len(rows),
        'diagnostic_hashes': hashes, 'paired_summary': rows, 'official': official,
        'full_validation': validation, 'unguarded_r2': 'built and reviewed; not measured or accepted; corrected before runtime trial'}
    write_exact(out / 'data/inherited-slot-proof-comparison-20261008.json', json.dumps(comparison, indent=2) + '\n')
    chart('Inherited slot diagnostic speed vs accepted R4 control',
          'Seven alternating process pairs; unchanged workloads; all four cases retained; diagnostic only.',
          [{'label': row['case'], 'speed': row['paired_control_over_candidate_speed'], 'color': '#2563eb'} for row in rows],
          'inherited-slot-proof-diagnostic-speed-20261008.svg', out)
    chart('Original SQLGlot parse speed vs CPython 3.14.7',
          'Original terminal pyperformance fast samples; saved CP/R4 references are descriptive, not paired.',
          [{'label': row['runtime'], 'speed': row['cpython_time_over_runtime_time_speed'],
            'color': '#64748b' if row['runtime'].startswith('cpython') else '#2563eb'} for row in official],
          'inherited-slot-proof-official-speed-20261008.svg', out)
    inherited = next(row for row in rows if row['case'] == 'inherited_slot')
    sql = next(row for row in rows if row['probe'] == 'sqlglot_parse')
    table = '\n'.join(f'| {r["case"]} | {r["cpython_reference_median_seconds"] * 1000:.3f} | '
        f'{r["control_process_median_seconds"] * 1000:.3f} | {r["candidate_process_median_seconds"] * 1000:.3f} | '
        f'{r["paired_control_over_candidate_speed"]:.3f}× | {r["pairs_favoring_candidate"]}/7 | '
        f'{r["descriptive_cpython_over_candidate_speed"]:.3f}× |' for r in rows)
    official_table = '\n'.join(f'| {r["runtime"]} | {r["mean_seconds"] * 1000:.4f} ± {r["sample_standard_deviation_seconds"] * 1000:.4f} | '
        f'{r["cpython_time_over_runtime_time_speed"]:.3f}× | {r["accepted_r4_time_over_runtime_time_speed"]:.3f}× | '
        f'{r["sample_count"]} | {r["pyperf_instability_warning"]} |' for r in official)
    candidate_official = next((row for row in official if row['runtime'] == 'inherited_r3_candidate'), None)
    official_caution = 'No validated official candidate measurement is scored.'
    if candidate_official is not None:
        official_caution = (f'The candidate original SQLGlot measurement has '
                            f'{candidate_official["coefficient_of_variation"] * 100:.1f}% sample variation, a '
                            f'{candidate_official["maximum_seconds"] * 1000:.3f} ms maximum, and instability-warning flag '
                            f'{candidate_official["pyperf_instability_warning"]}. Together with the neutral paired body, '
                            'these observations do not establish a reliable end-to-end parse gain.')
    report = f'''# Inherited slot declaration proof checkpoint — 2026-10-08

The initialized inherited-slot diagnostic improved **{inherited['paired_control_over_candidate_speed']:.3f}×** versus accepted R4, with **{inherited['pairs_favoring_candidate']}/7** favorable process pairs. The unchanged original SQLGlot-body diagnostic was **{sql['paired_control_over_candidate_speed']:.3f}×**, with **{sql['pairs_favoring_candidate']}/7** favorable pairs. The SQLGlot result is neutral: this trial does **not** establish a material parse improvement from the strong inherited-slot microbenchmark gain.

The runtime records immutable own declaration occurrences before inherited-name deduplication. Cold inherited promotion requires known history throughout the validated MRO, exactly one declaration, canonical descriptor owner/identity and matching owner/raw/effective receiver index. Existing own-slot proof remains. Native/serialized/late layout changes without history stay generic. Missing storage, aliases, duplicate/foreign declarations, hooks and lifetime cleanup keep their original dispatch. No pure-Python library implementation was replaced with C++.

R2 built successfully but its displaced-index proof was rejected during review **before runtime tests or measurements**. R3 requires the raw descriptor index to equal the receiver's effective index: generic VM reads check raw missing storage and may call `__getattr__` before name remapping. Its CPP fixture verifies that branch across nine real local/module VM reads, then the initialized-raw remapped result. R2 source/proposal/build evidence and the R3 correction/reviews/builds remain byte-preserved; R2 has no speed score or acceptance claim.

Candidate full validation status: **{status}**. {validation_text}

## Paired diagnostics

**1× means equal speed**, above 1× faster, below 1× slower. The control is preserved accepted R4 commit `e2a752f6`, with its complete 140-file manifest verified before and after measurement. All **300 samples and four rows** are retained. CPython observations are separate five-sample references and are not pooled into XLang3 pairs. Each XLang3 time is the median of seven process medians, each containing five samples. Paired speed is the median of seven within-pair ratios; CP-relative diagnostic speed is unpaired/descriptive. These are not official pyperformance scores or significance tests.

| Case | CP reference ms | R4 control ms | Candidate ms | Paired speed vs R4 | Favorable pairs | Diagnostic speed vs CP |
|---|---:|---:|---:|---:|---:|---:|
{table}

![Every paired diagnostic case](inherited-slot-proof-diagnostic-speed-20261008.svg)

The ordinary and own-slot rows are retained controls. Their observed movement does not identify its cause or justify attributing all improvement to inherited lookup. The full fixed Release gate remains required.

## Original official SQLGlot benchmark

Only measurement `values` from terminal original pyperformance fast JSON contribute to means. Warmups/calibration remain in the archive. The saved CPython full-fast reference and accepted R4 fast result are separate historical observations; candidate official results appear only after validated full correctness/gate phases with matching binary/source identities. Failed/partial output stays archived without a speed score. Instability warnings remain visible, and these reused references do not support paired significance claims.

| Runtime/reference | Mean ± sample SD ms | Speed vs CPython 3.14.7 | Speed vs accepted R4 | Samples | Instability warning |
|---|---:|---:|---:|---:|---|
{official_table}

{official_caution}

![Original official SQLGlot observations](inherited-slot-proof-official-speed-20261008.svg)

This is one generic runtime checkpoint, not a new complete 97-case run or a whole-suite CPython win. The SQLGlot body remains unchanged. CPython stays version **3.14.7** at `C:\\Python\\Python314\\python.exe`; build/run paths remain fixed.

## Evidence

- [Every diagnostic sample](data/inherited-slot-proof-paired-samples-20261008.csv)
- [All four diagnostic rows and seven ratios](data/inherited-slot-proof-paired-summary-20261008.csv)
- [Original official measurement samples](data/inherited-slot-proof-official-samples-20261008.csv)
- [Official means, variation and ratios](data/inherited-slot-proof-official-summary-20261008.csv)
- [Comparison, hashes and complete validation record](data/inherited-slot-proof-comparison-20261008.json)
- [Original-byte sources/logs/results/controls/reviews manifest](data/inherited-slot-proof-20261008-sources/manifest.json)

All raw archived copies preserve complete original bytes, lengths and SHA-256, including earlier failures. The 19 tested source/probe/controller hashes refer to compiled working bytes; separate Git-normalized LF copies for all 11 changed proposal targets have their own hashes and verified Git clean-filter blob identities. They do not replace the working-byte evidence. The archive stores Release manifests and verifies live control/candidate binaries; it does not duplicate binaries into documentation. Reference/control/candidate identities remain separate.
'''
    write_exact(out / (PREFIX + '.md'), report)
    for entry in inputs.values():
        require(sha(Path(entry['source']).read_bytes()) == entry['sha256'], 'Input moved during export')
    generated = [out / (PREFIX + '.md'), out / 'inherited-slot-proof-diagnostic-speed-20261008.svg',
                 out / 'inherited-slot-proof-official-speed-20261008.svg', *[out / 'data' / name for name in sorted(generated_data)]]
    manifest = {'status': 'terminal evidence archive', 'raw_files': inputs,
                'copy_policy': 'Full original bytes; no trimming or encoding/line-ending normalization',
                'control_binary_copy_policy': 'Existing preserved Release verified; documentation archives its manifest, not binaries',
                'exporter_sha256': sha(Path(__file__).read_bytes()),
                'generated_files': {path.relative_to(out).as_posix(): sha(path.read_bytes()) for path in generated}}
    write_exact(archive / 'manifest.json', json.dumps(manifest, indent=2) + '\n')
    print('Exported', len(samples), 'diagnostic samples and', len(inputs), 'complete raw files to', out)


if __name__ == '__main__':
    main()
