"""Export saved canonical-slot evidence only; never run a runtime, build or git.

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
PREFIX = 'canonical-slot-checkpoint-20261008'
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
    if stage == 'R4':
        require(record.get('terminal_record') and record.get('hashes_unchanged') and
                record['hashes_before'] == record['hashes_after'], 'R4 hashes changed or terminal receipt absent')
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
                'acceptance': 'rejected trial: inherited regression' if stage == 'R3' else 'pending validation'})
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
        default=ROOT / 'scratch/performance/canonical-slot-report-preview-20261008')
    args = parser.parse_args()
    require(sys.version_info[:3] == (3, 14, 7), 'Use CPython 3.14.7 for evidence export')
    require(Path.cwd().resolve() == ROOT.resolve(), 'Keep the run directory D:/CantorAI/xlang3')
    out = args.output_dir.resolve()
    scratch = (ROOT / 'scratch/performance').resolve()
    require(out == (ROOT / 'doc/performance').resolve() or out.is_relative_to(scratch),
            'Output must be the performance documentation directory or its scratch preview')
    archive = out / 'data/canonical-slot-20261008-sources'
    inputs, payloads = {}, {}
    validation_path = DATA / 'canonical-slot-r4-validation-20261008.json'
    validation = read_json(validation_path) if validation_path.exists() else None
    require(validation is None or validation['status'] != 'running', 'Full validation controller still runs')

    def keep(path, expected=None, archive_name=None):
        raw = path.read_bytes()
        digest = sha(raw)
        require(expected is None or digest == expected, 'Input hash mismatch: ' + str(path))
        name = archive_name or path.relative_to(ROOT).as_posix()
        require(not Path(name).is_absolute() and '..' not in Path(name).parts, 'Unsafe archive path')
        if name in inputs:
            require(inputs[name]['sha256'] == digest, 'Input changed during export')
        inputs[name] = {'source': str(path), 'sha256': digest, 'raw_bytes': len(raw)}
        payloads[name] = raw

    stages = {'R3': read_json(DATA / 'canonical-slot-paired-20261008.json'),
              'R4': read_json(DATA / 'canonical-slot-r4-paired-20261008.json')}
    early = read_json(DATA / 'canonical-slot-r4-early-20261008.json')
    require(early['status'] == 'terminal' and early.get('terminal_record') and early.get('hashes_unchanged'),
            'R4 focused correctness must be terminal')
    require(early['hashes_before'] == early['hashes_after'] == stages['R4']['hashes_before'],
            'R4 focused correctness and paired inputs differ')
    all_samples, all_rows = [], []
    for stage, record in stages.items():
        samples, rows = diagnostic_rows(stage, record)
        all_samples.extend(samples)
        all_rows.extend(rows)
    require(stages['R3']['binaries_sha256']['control'] ==
            stages['R4']['hashes_before']['binaries_sha256']['control'], 'Accepted control changed between revisions')
    require(stages['R3']['compatibility_hook_sha256'] ==
            stages['R4']['hashes_before']['compatibility_hook_sha256'], 'Diagnostic compatibility hook changed')
    r3_inherited = next(row for row in all_rows if row['stage'] == 'R3' and row['case'] == 'inherited_slot')
    require(r3_inherited['paired_control_over_candidate_speed'] < .9 and
            r3_inherited['pairs_favoring_candidate'] == 0, 'Expected retained R3 rejection evidence missing')
    for record in (early, stages['R4']):
        for phase in record.get('phases', []):
            require(phase['status'] != 'running', 'A phase still runs')
            for stream in ('stdout', 'stderr'):
                keep(DATA / phase[stream + '_log'], phase[stream + '_sha256'])
    for name, digest in stages['R4']['hashes_before']['source_sha256'].items():
        keep(ROOT / name, digest, 'r4-tested-inputs/' + name)
    original_source = Path(r'C:\Python\Python314\Lib\site-packages\pyperformance\data-files\benchmarks\bm_sqlglot_v2\run_benchmark.py')
    require(stages['R3']['benchmark_source_sha256'] ==
            stages['R4']['hashes_before']['benchmark_source_sha256'] == EXPECTED_SOURCE,
            'Original SQLGlot source changed')
    keep(original_source, EXPECTED_SOURCE, 'external/python314/bm_sqlglot_v2/run_benchmark.py')
    keep(ROOT / 'benchmarks/diagnostics/pyperf_compat/sitecustomize.py',
         stages['R4']['hashes_before']['compatibility_hook_sha256'])
    keep(ROOT / 'benchmarks/diagnostics/run_pyperformance_xlang3_shimmed.py')
    keep(ROOT / 'benchmarks/check_regression.py')

    official = []
    for runtime, prefix in OFFICIAL_PREFIXES.items():
        path, provenance_path, log_path = (DATA / (prefix + suffix) for suffix in ('.json', '-provenance.json', '.log'))
        provenance = read_json(provenance_path)
        require(provenance['status'] in ('finished', 'finished_with_benchmark_failures') and
                provenance['sha256_start'] == provenance['sha256_end'], 'Historical official reference not terminal/stable')
        require(provenance['mode'] == 'fast' and BENCHMARK not in provenance['failed_definitions'],
                'Official benchmark mode/failure mismatch')
        require(provenance['compatibility_hook_sha256'] ==
                stages['R4']['hashes_before']['compatibility_hook_sha256'], 'Official reference compatibility hook changed')
        source_hashes = {name.replace('\\', '/'): value for name, value in provenance['benchmark_python_sources'].items()}
        require(source_hashes['bm_sqlglot_v2/run_benchmark.py'] == EXPECTED_SOURCE, 'Historical original source mismatch')
        if runtime == 'cpython3147':
            require(provenance['runtime_version'] == '3.14.7' and
                    os.path.normcase(str(Path(provenance['runtime_executable']).resolve())) ==
                    os.path.normcase(str(Path(r'C:\Python\Python314\python.exe').resolve())), 'Wrong CPython reference')
            require(provenance['sha256_end']['exe'] ==
                    stages['R4']['hashes_before']['cpython3147_binary_sha256'], 'CPython binary differs from fresh reference')
        values = benchmark_values(read_json(path))
        official.append(official_row(runtime, path, log_path.read_text(encoding='utf-8'), values))
        for source in (path, provenance_path, log_path):
            keep(source)
    official_candidate_status = 'not measured: validation absent'
    validation_text = 'Full correctness and fixed regression gate have not been recorded.'
    if validation is not None:
        official_candidate_status = validation['status']
        require(validation['source_sha256'] == stages['R4']['hashes_before']['source_sha256'],
                'Full validation source inputs differ from R4 focused trial')
        for phase in validation['phases']:
            keep(DATA / phase['log'], phase['sha256'])
        counts = validation.get('fixture_counts', {})
        cpp_phase = next((phase for phase in validation['phases'] if phase['name'] == 'cpp'), None)
        cpp_count = None
        if cpp_phase is not None:
            cpp_log = (DATA / cpp_phase['log']).read_text(encoding='utf-8', errors='replace')
            matched = re.search(r'100% tests passed, 0 tests failed out of (\d+)', cpp_log)
            if matched:
                cpp_count = int(matched.group(1))
        validation_text = (f'Recorded full validation: {counts.get("core", "unavailable")} core fixtures, '
            f'{counts.get("compatibility_sections", "unavailable")} compatibility sections and '
            f'{counts.get("expected_failures", "unavailable")} expected-failure checks. '
            f'{cpp_count if cpp_count is not None else "Unavailable count of"} C++/SDK/graph tests are recorded; '
            'complete phase logs remain in the archive.')
        if 'fixed_gate' in validation:
            gate_path = DATA / validation['fixed_gate']['output']
            keep(gate_path, validation['fixed_gate']['sha256'])
            gate = read_json(gate_path)
            require(gate['status'] == 'pass' and len(gate['cases']) == 11 and
                    (gate['repeats'], gate['warmup'], gate['threshold']) == (21, 5, .1),
                    'The fixed regression gate must remain unchanged')
            validation_text += ' The unchanged fixed gate passed: 11 cases, 21 paired repeats, five warmups and 10% tolerance.'
        if validation['status'] == 'validated':
            require(cpp_phase is not None and cpp_phase['exit_code'] == 0 and cpp_count == 8,
                    'Validated checkpoint must retain all eight passing C++/SDK/graph tests')
            candidate = validation['official_sqlglot_parse']
            require(candidate['exit_code'] == 0 and candidate['sha256'], 'Official phase lacks terminal success/hash')
            phase = next(phase for phase in validation['phases'] if phase['name'] == 'official-sqlglot-parse')
            command = phase['command']
            require(phase['exit_code'] == 0 and '--benchmarks' in command and
                    command[command.index('--benchmarks') + 1] == BENCHMARK and '--mode' in command and
                    command[command.index('--mode') + 1] == 'fast', 'Score requires original pyperformance fast command')
            require(any(str(part).endswith('run_pyperformance_xlang3_shimmed.py') for part in command),
                    'Diagnostic probe cannot become official evidence')
            hashes = checkpoint_hashes(validation)
            candidate_hash = validation.get('candidate_binary_sha256', hashes.get('binaries_sha256', {}).get('candidate'))
            require(candidate_hash == stages['R4']['hashes_before']['binaries_sha256']['candidate'],
                    'Official candidate differs from R4 diagnostic candidate')
            path, log_path = DATA / candidate['output'], DATA / phase['log']
            keep(path, candidate['sha256'])
            keep(log_path, phase['sha256'])
            official.append(official_row('r4_candidate', path, log_path.read_text(encoding='utf-8'),
                                         benchmark_values(read_json(path))))
    cp_mean = official[0]['mean_seconds']
    for row in official:
        row['cpython_time_over_runtime_time_speed'] = cp_mean / row['mean_seconds']
        row['runtime_time_over_cpython_time'] = row['mean_seconds'] / cp_mean
    for row in all_rows:
        if row['stage'] == 'R4':
            row['acceptance'] = official_candidate_status + '; retains measured slower controls'

    # Preserve originals, failures, proposed/rejected revisions and all raw logs.
    # These copies retain CRLF, BOMs, whitespace and original encodings verbatim.
    for pattern in ('canonical-slot*', 'build-canonical-slot*', 'release-canonical-slot*',
                    'pyperformance-canonical-slot*'):
        for source in sorted(DATA.glob(pattern)):
            # Published output lives below DATA too. Never enumerate an archive
            # destination as source, including on an idempotent rerun.
            if source.resolve() == archive.resolve() or source.resolve().is_relative_to(archive.resolve()):
                continue
            if source.name in ('canonical-slot-paired-samples-20261008.csv',
                               'canonical-slot-paired-summary-20261008.csv',
                               'canonical-slot-official-samples-20261008.csv',
                               'canonical-slot-official-summary-20261008.csv',
                               'canonical-slot-checkpoint-comparison-20261008.json'):
                continue
            if source.is_file():
                keep(source)
            elif source.is_dir():
                for file in sorted(source.rglob('*')):
                    if file.is_file():
                        keep(file)
    for pattern in ('canonical-slot*', 'sqlglot-canonical-slot*', '*canonical-slot*20261008.py',
                    'canonical_slot_r4_support_20261008.py', '*canonical-slot*20261008.ps1'):
        for source in sorted((ROOT / 'scratch/performance').glob(pattern)):
            if source.is_file():
                keep(source)
            elif source.is_dir() and source.name == 'canonical-slot-r3-tested-sources-20261008':
                for file in sorted(source.rglob('*')):
                    if file.is_file():
                        keep(file)
    for control in ('vm-captured-lookup-checkpoint-20261007', 'canonical-slot-r3-trial-20261008'):
        keep(ROOT / 'build-repro/controls' / control / 'preserved-release-provenance.json')
    keep(Path(__file__).resolve())

    # Collect and check the complete inventory before creating any destination.
    # Raw copies cannot recursively become inputs while this script enumerates.
    for entry in inputs.values():
        require(sha(Path(entry['source']).read_bytes()) == entry['sha256'], 'Input changed during collection')
    for name, raw in payloads.items():
        write_exact(archive / name, raw)

    sample_fields = ['stage', 'probe', 'case', 'runtime', 'pair', 'process_order', 'sample', 'seconds', 'identity_json']
    write_exact(out / 'data/canonical-slot-paired-samples-20261008.csv', csv_bytes(sample_fields, all_samples))
    write_exact(out / 'data/canonical-slot-paired-summary-20261008.csv', csv_bytes(list(all_rows[0]), all_rows))
    official_samples = [{'runtime': row['runtime'], 'subtest': BENCHMARK, 'sample': index,
                         'seconds': seconds, 'source_json': row['source_json']}
                        for row in official for index, seconds in enumerate(row['values_seconds'], 1)]
    write_exact(out / 'data/canonical-slot-official-samples-20261008.csv',
                csv_bytes(['runtime', 'subtest', 'sample', 'seconds', 'source_json'], official_samples))
    official_summary = [{key: value for key, value in row.items() if key != 'values_seconds'} for row in official]
    write_exact(out / 'data/canonical-slot-official-summary-20261008.csv',
                csv_bytes(list(official_summary[0]), official_summary))
    comparison = {'status': 'terminal evidence export', 'official_candidate_status': official_candidate_status,
        'scope': 'Diagnostics separate from original official pyperformance; no whole-suite claim',
        'speed_convention': 'reference_seconds / runtime_seconds; 1x equal speed, above 1x faster',
        'historical_xlang3_is_not_the_paired_control': True,
        'diagnostic_samples': len(all_samples), 'diagnostic_summary_rows': len(all_rows),
        'diagnostic_binaries': {stage: checkpoint_hashes(record)['binaries_sha256'] for stage, record in stages.items()},
        'official': official, 'full_validation': validation, 'paired_summary': all_rows}
    write_exact(out / 'data/canonical-slot-checkpoint-comparison-20261008.json', json.dumps(comparison, indent=2) + '\n')
    chart('Canonical slot diagnostic speed vs accepted XLang3 control',
        'Seven alternating process pairs; every neutral/slower case retained. These are not official pyperformance scores.',
        [{'label': row['stage'] + ' ' + row['case'], 'speed': row['paired_control_over_candidate_speed'],
          'color': '#c2410c' if row['stage'] == 'R3' else '#2563eb'} for row in all_rows],
        'canonical-slot-diagnostic-speed-20261008.svg', out)
    chart('Official SQLGlot parse speed vs CPython 3.14.7',
        'Original terminal pyperformance fast samples only; saved references are historical descriptive comparisons.',
        [{'label': row['runtime'], 'speed': row['cpython_time_over_runtime_time_speed'],
          'color': '#64748b' if row['runtime'] == 'cpython3147' else '#2563eb'} for row in official],
        'canonical-slot-official-speed-20261008.svg', out)
    table = '\n'.join(f'| {r["stage"]} | {r["case"]} | {r["cpython_reference_median_seconds"] * 1000:.3f} | '
        f'{r["control_process_median_seconds"] * 1000:.3f} | {r["candidate_process_median_seconds"] * 1000:.3f} | '
        f'{r["paired_control_over_candidate_speed"]:.3f}× | {r["pairs_favoring_candidate"]}/7 | '
        f'{r["descriptive_cpython_over_candidate_speed"]:.3f}× |' for r in all_rows)
    official_table = '\n'.join(f'| {r["runtime"]} | {r["mean_seconds"] * 1000:.4f} | '
        f'{r["cpython_time_over_runtime_time_speed"]:.3f}× | {r["runtime_time_over_cpython_time"]:.2f}× | '
        f'{r["sample_count"]} | {r["pyperf_instability_warning"]} |' for r in official)
    inherited = next(row for row in all_rows if row['stage'] == 'R4' and row['case'] == 'inherited_slot')
    ordinary = next(row for row in all_rows if row['stage'] == 'R4' and row['case'] == 'ordinary_attr')
    report = f'''# Canonical slot read checkpoint — 2026-10-08

The runtime promotes proven initialized own slots to its existing class/version/index attribute cache. R4 also caches stable rejection of inherited, aliased and non-slot descriptor shapes. Missing/uninitialized storage, per-instance native hooks, invalid MRO and unsafe old owning cache values remain retryable. Changed descriptor installation clears weak property accessor flags before fresh guards and old-owner release. Owning Descriptor caches still clear on frame return. The Python benchmark/library code remains unchanged.

R3 was **rejected**: inherited-slot speed was **{r3_inherited['paired_control_over_candidate_speed']:.3f}×** the accepted XLang3 control, with **0/7** favorable pairs. Its own-slot improvement did not authorize acceptance. The original failed C++ run, repaired setup, complete rejected measurements, source snapshot and preserved Release manifest remain in the byte archive. R4 full validation status: **{official_candidate_status}**. No acceptance is inferred from a diagnostic improvement alone.

{validation_text}

## Paired diagnostics

Speed is reference time divided by runtime time: **1× means equal speed**, above 1× faster, below 1× slower. The control is the preserved accepted VM captured-lookup checkpoint. R3 and R4 CPython observations are separate five-sample references; they are never pooled into process pairs. Each XLang3 timing column is the median of seven process medians, with five samples per process. The paired speed column is the median of the seven within-pair ratios. CP-relative diagnostic speed is descriptive and unpaired.

All **600 raw samples** and **8 summary rows** remain, including inherited and ordinary controls. Both revisions use unchanged probes/work/checksums; SQLGlot uses its original body/input in a diagnostic runner. This does not score the full official benchmark or prove statistical significance.

| Revision | Case | CP reference ms | Control ms | Candidate ms | Paired speed vs control | Favorable pairs | Diagnostic speed vs CP |
|---|---|---:|---:|---:|---:|---:|---:|
{table}

![All paired diagnostic cases](canonical-slot-diagnostic-speed-20261008.svg)

R4 still has nominal slower controls: inherited reads are {inherited['paired_control_over_candidate_speed']:.3f}× with {inherited['pairs_favoring_candidate']}/7 favorable pairs, and ordinary reads are {ordinary['paired_control_over_candidate_speed']:.3f}× with {ordinary['pairs_favoring_candidate']}/7 favorable pairs. These results do not show that all regressions were eliminated. The ordinary attribute path warms InstanceAttr and bypasses descriptor eligibility; retain its unchanged no-regression gate. A change in its timing cannot be attributed to the negative marker from source inspection alone.

## Original official pyperformance

Only measured `values` from terminal original pyperformance JSON contribute here. Calibration and warmups remain in the raw archive but are excluded from means. The saved 20261007 full-fast CPython 3.14.7 and XLang3 references are historical; the historical XLang3 binary is **not** the accepted alternating-pair control. R4 appears only after successful terminal original SQLGlot validation with matching candidate hashes. Failed or partial candidate output is preserved without a speed score.

| Runtime/reference | Mean ms | Speed vs CPython 3.14.7 | Time vs CPython | Samples | Instability warning |
|---|---:|---:|---:|---:|---|
{official_table}

![Original official SQLGlot results](canonical-slot-official-speed-20261008.svg)

These reused official references give descriptive comparisons, not alternating official-pair significance. This checkpoint is not a new complete 97-case run or a whole-suite CPython win. No diagnostic, IR inspection, profiling or incomplete GC work is substituted for official scores.

## Evidence

- [Every diagnostic sample](data/canonical-slot-paired-samples-20261008.csv)
- [All diagnostic summary rows and per-pair ratios](data/canonical-slot-paired-summary-20261008.csv)
- [Original official measurement values](data/canonical-slot-official-samples-20261008.csv)
- [Official means, variation and speed ratios](data/canonical-slot-official-summary-20261008.csv)
- [Complete comparison, hashes and validation record](data/canonical-slot-checkpoint-comparison-20261008.json)
- [Byte-preserved raw sources/logs/results manifest](data/canonical-slot-20261008-sources/manifest.json)

All archived file copies retain their complete original bytes, with SHA-256 and lengths. Reference/control/candidate identities remain separate. Build and run paths stay fixed; benchmark comparison uses CPython 3.14.7 at `C:\\Python\\Python314\\python.exe`.
'''
    write_exact(out / (PREFIX + '.md'), report)
    for entry in inputs.values():
        require(sha(Path(entry['source']).read_bytes()) == entry['sha256'], 'Input changed during export')
    generated = [out / (PREFIX + '.md'), out / 'canonical-slot-diagnostic-speed-20261008.svg',
                 out / 'canonical-slot-official-speed-20261008.svg',
                 *[out / 'data' / name for name in ('canonical-slot-paired-samples-20261008.csv',
                     'canonical-slot-paired-summary-20261008.csv', 'canonical-slot-official-samples-20261008.csv',
                     'canonical-slot-official-summary-20261008.csv', 'canonical-slot-checkpoint-comparison-20261008.json')]]
    manifest = {'status': 'terminal evidence archive', 'raw_files': inputs,
        'copy_policy': 'Complete original bytes, no trimming/encoding/line-ending normalization',
        'exporter_sha256': sha(Path(__file__).read_bytes()),
        'generated_files': {path.relative_to(out).as_posix(): sha(path.read_bytes()) for path in generated}}
    write_exact(archive / 'manifest.json', json.dumps(manifest, indent=2) + '\n')
    print('Exported', len(all_samples), 'diagnostic samples and', len(inputs), 'complete raw files to', out)


if __name__ == '__main__':
    main()
