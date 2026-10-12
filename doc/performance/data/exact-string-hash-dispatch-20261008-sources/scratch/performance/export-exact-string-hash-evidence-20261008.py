"""Export frozen String hash evidence, never launch runtimes or measurements.

Prepared only. Default output is an isolated fresh scratch preview. Inputs are
explicit, buffered before writes, and copied without trimming/newline conversion.
Historical candidate binaries are identified by terminal receipts, not live paths.
"""
import argparse
import csv
import hashlib
import html
import io
import json
import math
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
STEM = 'exact-string-hash-dispatch-20261008'
PATHS = ('string_dict_get', 'python_key_dict_get', 'builtin_hash_string', 'builtin_hash_python_key',
         'direct_python_hash_method', 'saved_python_hash_method', 'ordinary_python_hash_function',
         'ordinary_python_hash_wrapper')


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def json_bytes(document):
    return (json.dumps(document, indent=2) + '\n').encode('utf-8')


def csv_bytes(fields, rows):
    buffer = io.StringIO(newline='')
    writer = csv.DictWriter(buffer, fieldnames=fields, lineterminator='\n')
    writer.writeheader()
    writer.writerows(rows)
    return buffer.getvalue().encode('utf-8')


def official_values(document):
    selected = [row for row in document['benchmarks'] if
                row.get('metadata', {}).get('name', document.get('metadata', {}).get('name')) == 'sqlglot_v2_parse']
    assert len(selected) == 1
    values = [value for run in selected[0]['runs'] for value in run.get('values', [])]
    assert len(values) == 20 and all(math.isfinite(value) and value > 0 for value in values)
    return values


def horizontal_chart(title, subtitle, rows, max_speed):
    left, width, step = 330, 540, 43
    height = 112 + step * len(rows)
    elements = [f'<svg xmlns="http://www.w3.org/2000/svg" width="1080" height="{height}" viewBox="0 0 1080 {height}">',
        '<rect width="100%" height="100%" fill="#ffffff"/>',
        '<g font-family="Arial, sans-serif" fill="#172b4d">',
        f'<text x="20" y="28" font-size="20" font-weight="bold">{html.escape(title)}</text>',
        f'<text x="20" y="51" font-size="13">{html.escape(subtitle)}</text>']
    for tick in (0, max_speed / 2, max_speed):
        x = left + width * tick / max_speed
        elements.append(f'<line x1="{x}" x2="{x}" y1="71" y2="{height-32}" stroke="#e0e6ed"/>')
        elements.append(f'<text x="{x}" y="{height-12}" text-anchor="middle" font-size="12">{tick:g}x</text>')
    x_one = left + width / max_speed
    elements.append(f'<line x1="{x_one}" x2="{x_one}" y1="71" y2="{height-32}" stroke="#44566c" stroke-dasharray="5 4"/>')
    for index, (label, speed, detail, color) in enumerate(rows):
        y = 79 + step * index
        bar = width * speed / max_speed
        elements.extend([f'<text x="20" y="{y+19}" font-size="13">{html.escape(label)}</text>',
            f'<rect x="{left}" y="{y}" width="{bar:.3f}" height="27" rx="3" fill="{color}"/>',
            f'<text x="{left+bar+10:.3f}" y="{y+19}" font-size="13">{html.escape(detail)}</text>'])
    return ('\n'.join(elements) + '\n</g></svg>\n').encode('utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir', type=Path,
                        default=ROOT / 'scratch/performance/exact-string-hash-export-preview-20261008')
    parser.add_argument('--string-source', type=Path, default=ROOT / 'src/runtime/value_hash.cpp')
    args = parser.parse_args()
    out = args.output_dir.resolve()
    archive = Path('data') / (STEM + '-sources')
    raw_inputs, input_receipts, source_inputs = {}, [], {}

    def keep(path, expected=None, alias=None):
        path = Path(path).resolve(strict=True)
        raw = path.read_bytes()
        if expected is not None: assert sha(raw) == expected, str(path)
        name = alias or path.relative_to(ROOT).as_posix()
        assert name not in raw_inputs or raw_inputs[name] == raw
        raw_inputs[name] = raw
        source_inputs[path] = raw
        input_receipts.append({'source': str(path), 'archive_path': (archive / name).as_posix(),
                               'sha256': sha(raw), 'bytes': len(raw)})
        return raw

    def read(path):
        return json.loads(keep(path))

    paired = read(DATA / 'exact-string-hash-paired-20261008.json')
    assert paired['status'] == 'terminal_diagnostic_only' and paired['terminal'] and paired['hashes_unchanged']
    assert (paired['pairs'], paired['samples_per_pair']) == (7, 5) and len(paired['raw']) == 14
    keep(ROOT / 'scratch/performance/python-hash-callback-cost-probe-20261008.py', paired['source_sha256'])
    keep(args.string_source, paired['engine_source_sha256'], 'compiled-string-source/value_hash.cpp')
    applied = read(DATA / 'exact-string-hash-applied-source-20261008.json')
    assert applied['files']['src/runtime/value_hash.cpp']['working_sha256'] == paired['engine_source_sha256']
    keep(ROOT / 'scratch/performance/exact-string-hash-dispatch-proposed-20261008.patch', applied['patch_sha256'])
    control_manifest_path = ROOT / 'build-repro/controls/inherited-slot-proof-checkpoint-20261008/preserved-release-provenance.json'
    control_manifest = json.loads(keep(control_manifest_path, applied['accepted_control_manifest_sha256']))
    assert control_manifest['accepted'] and control_manifest['commit'].startswith('9de05e0d')
    original = read(DATA / 'python-hash-callback-cost-20261008.json')
    assert original['status'] == 'terminal_diagnostic_only' and original['source_sha256'] == paired['source_sha256']
    cp_diagnostic = original['results']['cpython3147']
    assert cp_diagnostic['runtime'] == 'cpython' and cp_diagnostic['version'].startswith('3.14.7 ')
    assert json.loads(keep(DATA / 'python-hash-callback-cost-cpython3147-20261008.log')) == cp_diagnostic
    keep(DATA / 'python-hash-callback-cost-accepted_xlang3-20261008.log')
    keep(DATA / 'exact-string-hash-accepted-control-reference-20261008.log')
    reference = read(DATA / 'exact-string-hash-cpython3147-reference-20261008.json')
    assert reference['exit_code'] == 0
    keep(DATA / 'exact-string-hash-cpython3147-reference-20261008.stdout.log', reference['stdout_sha256'])
    keep(DATA / 'exact-string-hash-cpython3147-reference-20261008.stderr.log', reference['stderr_sha256'])
    keep(ROOT / 'scratch/performance/exact-string-hash-fixture-proposed-20261008.py', reference['source_sha256'])

    samples, by_pair = [], {}
    for order, run in enumerate(paired['raw']):
        pair, runtime, result = run['pair'], run['runtime'], run['result']
        assert pair in range(7) and runtime in ('control', 'candidate') and (pair, runtime) not in by_pair
        assert result['runtime'] == 'xlang3' and result['instrumentation'] == 'none'
        assert result['warmup_operations_per_row'] == 256 and result['operations_per_sample'] == 16384
        assert tuple(row['path'] for row in result['rows']) == PATHS
        assert json.loads(keep(DATA / run['log'], run['log_sha256'])) == result
        by_pair[pair, runtime] = result
        for row in result['rows']:
            assert row['operations'] == 16384 and len(row['samples_seconds']) == 5
            for index, seconds in enumerate(row['samples_seconds']):
                assert math.isfinite(seconds) and seconds > 0
                samples.append({'scope': 'paired_diagnostic', 'runtime': runtime, 'pair': pair,
                    'process_position': order % 2, 'path': row['path'], 'sample': index,
                    'seconds': seconds, 'operations': row['operations'], 'checksum': row['checksum']})
    cp_rows = {row['path']: row for row in cp_diagnostic['rows']}
    assert tuple(cp_rows) == PATHS
    for row in cp_rows.values():
        assert len(row['samples_seconds']) == 5 and row['operations'] == 16384
        for index, seconds in enumerate(row['samples_seconds']):
            assert math.isfinite(seconds) and seconds > 0
            samples.append({'scope': 'historical_cpython_diagnostic_unpaired', 'runtime': 'cpython3147', 'pair': '',
                'process_position': '', 'path': row['path'], 'sample': index, 'seconds': seconds,
                'operations': row['operations'], 'checksum': row['checksum']})
    medians, ratios = [], []
    for index, name in enumerate(PATHS):
        control, candidate, pair_ratios = [], [], []
        for pair in range(7):
            a, b = (by_pair[pair, runtime]['rows'][index] for runtime in ('control', 'candidate'))
            assert all(a[key] == b[key] for key in ('path', 'operations', 'checksum', 'key_text'))
            ratio = statistics.median(a['samples_seconds']) / statistics.median(b['samples_seconds'])
            pair_ratios.append(ratio)
            ratios.append({'path': name, 'pair': pair, 'control_over_candidate_speed': ratio})
            control.extend(a['samples_seconds'])
            candidate.extend(b['samples_seconds'])
        cp_median, control_median, candidate_median = map(statistics.median, (cp_rows[name]['samples_seconds'], control, candidate))
        speed = statistics.median(pair_ratios)
        published = next(row for row in paired['summary'] if row['path'] == name)
        assert math.isclose(speed, published['control_over_candidate_speed'], rel_tol=1e-12)
        medians.append({'path': name, 'cpython_historical_us_per_op': cp_median * 1e6 / 16384,
            'control_us_per_op': control_median * 1e6 / 16384, 'candidate_us_per_op': candidate_median * 1e6 / 16384,
            'paired_speed_control_1x': speed, 'faster_pairs': sum(ratio > 1 for ratio in pair_ratios),
            'candidate_vs_historical_cp_speed': cp_median / candidate_median})

    cp_full_path = DATA / 'pyperformance-cpython3147-live-eval-full-fast-20261007.json'
    cp_full = read(cp_full_path)
    cp_provenance = read(cp_full_path.with_name(cp_full_path.stem + '-provenance.json'))
    assert cp_provenance['status'] == 'finished' and cp_provenance['runtime_version'] == '3.14.7'
    official, official_samples, receipts = {}, [], {}
    official['cpython3147_saved'] = official_values(cp_full)
    for label, prefix in [('control', 'exact-string-hash-control-sqlglot-20261008'),
                          ('candidate', 'exact-string-hash-official-sqlglot-20261008')]:
        receipt = read(DATA / (prefix + '-receipt.json'))
        assert receipt['status'] == 'terminal' and receipt['terminal'] and receipt['exit_code'] == 0 and receipt['hashes_unchanged']
        document = json.loads(keep(DATA / (prefix + '.json'), receipt['output_sha256']))
        keep(DATA / (prefix + '.log'), receipt['log_sha256'])
        for path, digest in receipt['hashes_before'].items():
            if Path(path).name in ('xlang3.exe', 'xlang3_runtime.dll'):
                assert paired['binaries_sha256'][path] == digest
            elif Path(path).name == 'sitecustomize.py':
                assert digest == cp_provenance['compatibility_hook_sha256']
                keep(path, digest)
            elif Path(path).name == 'run_benchmark.py':
                assert digest == cp_provenance['benchmark_python_sources']['bm_sqlglot_v2\\run_benchmark.py']
                keep(path, digest, 'official-benchmark-source/bm_sqlglot_v2/run_benchmark.py')
        official[label] = official_values(document)
        receipts[label] = receipt
    stats = {}
    for runtime, values in official.items():
        mean, deviation = statistics.mean(values), statistics.stdev(values)
        stats[runtime] = {'mean_ms': mean * 1000, 'stdev_ms': deviation * 1000,
                         'cv_percent': deviation / mean * 100, 'max_ms': max(values) * 1000, 'count': 20}
        for index, seconds in enumerate(values):
            official_samples.append({'runtime': runtime, 'sample': index, 'seconds': seconds})
    ratio = stats['control']['mean_ms'] / stats['candidate']['mean_ms']
    cp_speed = stats['cpython3147_saved']['mean_ms'] / stats['candidate']['mean_ms']
    for relative in ('scratch/performance/measure-exact-string-hash-paired-20261008.py',
                     'scratch/performance/check-exact-string-hash-official-20261008.py',
                     'scratch/performance/check-exact-string-hash-control-official-20261008.py',
                     'scratch/performance/exact-string-hash-dispatch-review-20261008.txt',
                     'scratch/performance/exact-string-hash-dispatch-provenance-20261008.json',
                     'doc/performance/data/build-exact-string-hash-Release-20261008.log'):
        keep(ROOT / relative)
    keep(Path(__file__))

    generated = {}
    generated['data/' + STEM + '-diagnostic-samples.csv'] = csv_bytes(list(samples[0]), samples)
    generated['data/' + STEM + '-diagnostic-medians.csv'] = csv_bytes(list(medians[0]), medians)
    generated['data/' + STEM + '-paired-ratios.csv'] = csv_bytes(list(ratios[0]), ratios)
    generated['data/' + STEM + '-official-values.csv'] = csv_bytes(list(official_samples[0]), official_samples)
    paired_chart = STEM + '-paired.svg'
    generated['data/' + paired_chart] = horizontal_chart('Generic hash diagnostic: candidate versus accepted XLang3',
        'Accepted control = 1x throughput; seven alternating process pairs, five samples per row per process',
        [(row['path'], row['paired_speed_control_1x'], f"{row['paired_speed_control_1x']:.3f}x ({row['faster_pairs']}/7 pairs)", '#2474a6') for row in medians], 2)
    official_chart = STEM + '-official-vs-cpython.svg'
    generated['data/' + official_chart] = horizontal_chart('Original official SQLGlot parse: throughput versus CPython 3.14.7',
        'Saved CPython = 1x; both fresh XLang3 series remain much slower. Twenty values per series; unpaired comparison.',
        [('CPython 3.14.7 (saved)', 1, '1.000x', '#2474a6'),
         ('Accepted XLang3 (fresh)', stats['cpython3147_saved']['mean_ms'] / stats['control']['mean_ms'],
          f"{stats['cpython3147_saved']['mean_ms']/stats['control']['mean_ms']:.3f}x", '#b65151'),
         ('String-dispatch candidate (fresh)', cp_speed, f'{cp_speed:.3f}x ({1/cp_speed:.2f}x slower)', '#b65151')], 1)
    table = ['| Diagnostic path | CP historical us/op | Control us/op | Candidate us/op | Paired speed, control=1x | Faster pairs |',
             '|---|---:|---:|---:|---:|---:|']
    table += [f"| {row['path']} | {row['cpython_historical_us_per_op']:.4f} | {row['control_us_per_op']:.4f} | {row['candidate_us_per_op']:.4f} | {row['paired_speed_control_1x']:.3f}x | {row['faster_pairs']}/7 |" for row in medians]
    official_table = ['| Original sqlglot_v2_parse | Mean +/- SD (ms) | CV | Max (ms) | Values |',
                      '|---|---:|---:|---:|---:|']
    official_table += [f"| {runtime} | {row['mean_ms']:.4f} +/- {row['stdev_ms']:.4f} | {row['cv_percent']:.2f}% | {row['max_ms']:.4f} | 20 |" for runtime, row in stats.items()]
    source_link = (archive / 'compiled-string-source/value_hash.cpp').as_posix()
    paired_link = (archive / 'doc/performance/data/exact-string-hash-paired-20261008.json').as_posix()
    report = [
        '# Exact string hash dispatch evidence — 2026-10-08', '',
        f"The cached-string `hash()` diagnostic improved **{next(row['paired_speed_control_1x'] for row in medians if row['path']=='builtin_hash_string'):.3f}x** versus the accepted XLang3 control. The original official SQLGlot parse comparison is **neutral**: its nominal control/candidate ratio is {ratio:.4f}x, with candidate CV {stats['candidate']['cv_percent']:.2f}% and an instability warning. This is a focused runtime improvement, with no whole-suite or CPython win claim.", '',
        'Exact StringObject hashing now reads the existing immutable hash cache before integer-payload conversion. This avoids four guaranteed failed generic attribute lookups. Python str subclasses, numeric payload instances and their callbacks retain the ordinary path. No Python library body was replaced.', '',
        'Speed convention: 1x is equal throughput; greater than 1x is faster. Diagnostic ratios use the median of seven control/candidate ratios, each based on the five within-process samples. All eight rows, 560 paired values and 40 historical CPython values are retained; no outlier is removed. CPython diagnostic references are historical and unpaired.', '',
        f'![Paired diagnostic throughput](data/{paired_chart})', '', *table, '',
        'The string dictionary lookup is a comparison control; one of seven pairs was slower despite the nominal median above 1x. Improvements in Python-key rows include their original Python call/frame work, not isolated native primitive time.', '',
        f'![Official throughput versus CPython](data/{official_chart})', '', *official_table, '',
        f"The candidate remains **{1/cp_speed:.2f}x slower** than saved CPython 3.14.7 on the original official parse case ({cp_speed:.3f}x throughput). Both fresh XLang3 logs retain pyperf's instability warnings. Fresh control/candidate official runs are sequential, unpaired series; the ~{(ratio-1)*100:.2f}% nominal difference is not evidence of a meaningful end-to-end gain.", '',
        'Scope and provenance: the accepted control is the preserved 9de05e0d checkpoint. The candidate is the String-dispatch build with already pending SQLite R5/R6 work; this report does not attribute an entire runtime comparison to one source hunk. Receipts identify the exact timed EXE/DLL bytes even after the fixed live build path is rebuilt. The control official receipt also hashes the current workspace String candidate source; that entry is not claimed as the source compiled into the control binary.', '',
        'Correctness status at this component measurement: CPython passed the unchanged five-group hash fixture. The accepted XLang3 control passed its first three groups and failed preservation of a custom __hash__ exception. That pre-existing failure is retained, and a separate exception-preservation proposal addresses it without weakening the fixture. Combined correctness/gate validation belongs to the later combined checkpoint; this component report is not an accepted complete checkpoint.', '',
        f'[Raw paired receipt]({paired_link}) · [Exact compiled String source]({source_link})', '',
        'The archive preserves explicit source, scripts, JSON receipts and full log bytes without trimming. Official values come only from terminal original pyperformance JSON; diagnostic values and historical CPython references remain separate.', '']
    generated[STEM + '.md'] = '\n'.join(report).encode('utf-8')
    generated['data/' + STEM + '-summary.json'] = json_bytes({'status': 'terminal_evidence_export',
        'diagnostic_only_pairs': medians, 'official_unpaired': stats,
        'official_nominal_control_over_candidate': ratio, 'official_conclusion': 'neutral',
        'candidate_vs_saved_cpython_speed': cp_speed, 'diagnostic_values': len(samples),
        'official_values': len(official_samples), 'input_receipts': input_receipts})
    # Freeze the COMPLETE explicit inventory before the first output write.
    payload = {(archive / name).as_posix(): raw for name, raw in raw_inputs.items()}
    payload.update(generated)
    manifest = {'status': 'frozen_evidence_export', 'output_dir': str(out),
        'scope': 'Eight paired diagnostics and one original official case; no runtime or benchmark launched',
        'files': [{'path': name, 'sha256': sha(raw), 'bytes': len(raw)} for name, raw in sorted(payload.items())]}
    manifest_name = 'data/' + STEM + '-export-manifest.json'
    assert not any((out / name).exists() for name in [*payload, manifest_name]), 'Refuse overwriting evidence'
    assert all(path.read_bytes() == raw for path, raw in source_inputs.items()), 'Input changed during read'
    for name, raw in payload.items():
        target = out / name
        target.parent.mkdir(parents=True, exist_ok=True)
        with target.open('xb') as stream: stream.write(raw)
    with (out / manifest_name).open('xb') as stream: stream.write(json_bytes(manifest))
    print('Prepared export:', len(samples), 'diagnostic values,', len(official_samples),
          'official values,', len(raw_inputs), 'explicit raw files; no measurements executed')


if __name__ == '__main__':
    main()
