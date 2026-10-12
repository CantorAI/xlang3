"""Export saved lazy-globals trial evidence only; never launch a workload or stage Git."""
import argparse
import csv
import hashlib
import io
import json
import math
from pathlib import Path
import statistics
import sys

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
SCRATCH = ROOT / 'scratch/performance'
PREFIX = 'interpreter-lazy-globals-trial-20261008'
ARCHIVE = DATA / (PREFIX + '-sources')
CP_STEM = 'pyperformance-cpython3147-live-eval-full-fast-20261007'
SOURCES = ['src/internal/xlang3/interpreter.h',
    'src/executor/xlang_vm/ops/xlang_vm_ops_variables.h',
    'src/executor/xlang_vm/ops/xlang_vm_ops_import_raw.h',
    'src/executor/xlang_vm/ops/xlang_vm_ops_fused.h',
    'src/executor/xlang_vm/ops/xlang_vm_ops_call.h',
    'tests/cpp/interpreter_tests.cpp', 'tests/cpp/interpreter_lazy_globals_cases.h']

def sha(raw): return hashlib.sha256(raw).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--decision', choices=('held', 'rejected', 'accepted'), default='held')
    decision = parser.parse_args().decision
    assert sys.version_info[:3] == (3, 14, 7)
    assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
    inputs = {}
    def keep(path, expected=None, archive=True):
        path = Path(path).resolve(strict=True)
        raw = path.read_bytes()
        assert expected is None or sha(raw) == expected, str(path)
        assert path not in inputs or inputs[path]['raw'] == raw
        inputs[path] = {'raw': raw, 'sha256': sha(raw), 'archive': archive}
        return raw
    def read(path, expected=None): return json.loads(keep(path, expected).decode('utf-8-sig'))
    validation_path = DATA / 'interpreter-lazy-globals-validation-20261008.json'
    validation = read(validation_path)
    assert validation['status'] == 'validated' and validation['terminal'] and validation['hashes_unchanged']
    assert validation['correctness_passed'] and all(p['exit_code'] == 0 and p['passed'] for p in validation['phases'])
    assert validation['fixture_counts'] == {'core': 381, 'compatibility_sections': 11, 'expected_failures': 3}
    inventory = read(validation['source_inventory'], validation['source_inventory_sha256'])
    source_hashes = inventory['source_sha256']
    assert inventory['owned_sources'] == SOURCES
    assert len(source_hashes) == 40 and source_hashes == validation['source_sha256']
    for name, expected in source_hashes.items(): keep(ROOT / name, expected)
    for name, expected in validation['binaries_sha256'].items(): keep(ROOT / name, expected, archive=False)
    for phase in validation['phases']:
        keep(DATA / phase['stdout_log'], phase['stdout_sha256'])
        keep(DATA / phase['stderr_log'], phase['stderr_sha256'])
    gate = read(DATA / validation['fixed_gate']['output'], validation['fixed_gate']['sha256'])
    assert gate['status'] == 'pass' and len(gate['cases']) == 11
    assert (gate['repeats'], gate['warmup'], gate['threshold']) == (21, 5, .1)
    for name, row in gate['cases'].items(): assert row['source_sha256'] == validation['fixed_gate_source_sha256'][name]
    for reference in validation['preserved_cpython3147_focused']:
        receipt = read(DATA / reference['record'], reference['sha256'])
        if 'stdout_log' in reference:
            keep(DATA / reference['stdout_log'], receipt['stdout_sha256'])
            keep(DATA / reference['stderr_log'], receipt['stderr_sha256'])
        elif 'phase' in reference:
            keep(DATA / reference['phase']['log'], reference['phase']['sha256'])
        else:
            keep(DATA / 'sqlite-cursor-r6-cpython3147-20261008.log', receipt['log_sha256'])
    crash = validation['preserved_cpython_guard_crash_reference']
    isolated = read(DATA / crash['output'], crash['sha256'])
    for case in isolated['cases']:
        for kind in ('stdout', 'stderr'):
            keep(DATA / case[kind + '_file'], case[kind + '_sha256'])
    cp_meta = read(DATA / (CP_STEM + '-provenance.json'), validation['preserved_official_cpython3147']['provenance_sha256'])
    cp_data = read(DATA / (CP_STEM + '.json'), validation['preserved_official_cpython3147']['sha256'])
    keep(DATA / (CP_STEM + '.log'))
    assert cp_meta['status'] == 'finished' and cp_meta['runtime_version'] == '3.14.7'
    assert cp_meta['sha256_start'] == cp_meta['sha256_end']
    assert cp_meta['sha256_end']['exe'] == validation['cpython3147_binary_sha256']
    assert cp_meta['sha256_end']['dll'] == validation['cpython3147_dll_sha256']
    assert cp_meta['compatibility_hook_sha256'] == validation['compatibility_hook_sha256']
    paired_path = DATA / 'interpreter-lazy-globals-paired-20261008.json'
    paired = read(paired_path)
    assert paired['terminal'] and paired['hashes_unchanged'] and paired['status'] == 'terminal_diagnostic_only'
    assert paired['source_hashes_unchanged']
    assert paired['pairs'] == 7 and paired['samples_per_pair'] == 5 and len(paired['raw']) == 14
    assert paired['engine_source_sha256'] == source_hashes[SOURCES[0]]
    for path, expected in paired['binaries_sha256'].items(): keep(path, expected, archive=False)
    for name in ('xlang3.exe', 'xlang3_runtime.dll'):
        path = ROOT / 'build-repro/main-verify-20261006/Release' / name
        assert paired['binaries_sha256'][str(path)] == validation['binaries_sha256'][path.relative_to(ROOT).as_posix()]
    paired_values, paired_rows = [], []
    for item in paired['raw']:
        keep(DATA / item['log'], item['log_sha256'])
        assert item['result']['instrumentation'] == 'none' and len(item['result']['rows']) == 8
        for row in item['result']['rows']:
            assert row['operations'] == 16384 and len(row['samples_seconds']) == 5
            for sample, value in enumerate(row['samples_seconds']):
                assert math.isfinite(value) and value > 0
                paired_values.append({'path': row['path'], 'pair': item['pair'], 'runtime': item['runtime'],
                    'sample': sample, 'seconds': value, 'operations': row['operations'], 'checksum': row['checksum']})
    for index, row in enumerate(paired['summary']):
        ratios = []
        for pair in range(7):
            left, right = [next(item['result']['rows'][index] for item in paired['raw']
                if item['pair'] == pair and item['runtime'] == label) for label in ('control', 'candidate')]
            assert all(left[k] == right[k] for k in ('path', 'operations', 'checksum', 'key_text'))
            ratios.append(statistics.median(left['samples_seconds']) / statistics.median(right['samples_seconds']))
        assert ratios == row['pair_ratios'] and statistics.median(ratios) == row['control_over_candidate_speed']
        paired_rows.append({'path': row['path'], 'median_pair_control_over_candidate': statistics.median(ratios),
            'pair_ratios': ';'.join(map(str, ratios)), 'control_pooled_median_seconds': row['control_median_seconds'],
            'candidate_pooled_median_seconds': row['candidate_median_seconds']})
    assert len(paired_values) == 560
    for relative in ('scratch/performance/interpreter-lazy-globals-zeroargc-actual-rebased-20261008.patch',
        'scratch/performance/interpreter-lazy-globals-zeroargc-actual-rebased-20261008-provenance.json',
        'scratch/performance/interpreter-lazy-globals-r2-proposal-20261008.patch',
        'scratch/performance/interpreter-lazy-globals-r2-proposal-20261008-provenance.json',
        'scratch/performance/interpreter-lazy-globals-zeroargc-actual-rebased-review-20261008.md',
        'scratch/performance/measure-interpreter-lazy-globals-paired-20261008.py',
        'scratch/performance/python-hash-callback-cost-probe-20261008.py',
        'scratch/performance/validate-interpreter-lazy-globals-20261008.py',
        'scratch/performance/apply-interpreter-lazy-globals-trial-20261008.py',
        'doc/performance/data/build-interpreter-lazy-globals-Release-20261008.log',
        'build-repro/controls/native-bound-zero-args-checkpoint-20261008/preserved-release-provenance.json'):
        keep(ROOT / relative)
    keep(Path(__file__))
    assert sha(keep(SCRATCH / 'python-hash-callback-cost-probe-20261008.py')) == paired['source_sha256']
    official_values, official_rows, warning_lines = [], [], []
    def values(document, benchmark):
        rows = [r for r in document['benchmarks'] if r.get('metadata', {}).get('name', document.get('metadata', {}).get('name')) == benchmark]
        assert len(rows) == 1
        data = [v for run in rows[0]['runs'] for v in run.get('values', [])]
        assert len(data) == 20 and all(math.isfinite(v) and v > 0 for v in data)
        return data
    for benchmark, key in [('sqlite_synth', 'official_sqlite_synth'), ('sqlglot_v2_parse', 'official_sqlglot_parse')]:
        receipt = validation[key]
        assert receipt['complete'] and receipt['exit_code'] == 0 and receipt['values_count'] == 20
        candidate = read(DATA / receipt['output'], receipt['sha256'])
        cp_values = values(cp_data, benchmark)
        for runtime, measured in [('cpython3147_saved', cp_values), ('xlang3_candidate', values(candidate, benchmark))]:
            for sample, seconds in enumerate(measured): official_values.append({'benchmark': benchmark, 'runtime': runtime, 'sample': sample, 'seconds': seconds})
            mean, sd = statistics.mean(measured), statistics.stdev(measured)
            official_rows.append({'benchmark': benchmark, 'runtime': runtime, 'values': 20,
                'mean_seconds': mean, 'sample_sd_seconds': sd, 'cv_percent': 100 * sd / mean,
                'speed_vs_cpython': statistics.mean(cp_values) / mean})
        phase = next(p for p in validation['phases'] if p['name'] == ('official-sqlite-synth' if benchmark == 'sqlite_synth' else 'official-sqlglot-v2-parse'))
        for kind in ('stdout', 'stderr'):
            text = inputs[(DATA / phase[kind + '_log']).resolve()]['raw'].decode('utf-8', errors='replace')
            warning_lines.extend(line for line in text.splitlines() if any(word in line.lower() for word in ('warning', 'unstable', 'standard deviation', 'not enough samples')))
    generated = {}
    def csv_file(name, rows):
        stream = io.StringIO(newline=''); writer = csv.DictWriter(stream, fieldnames=list(rows[0]), lineterminator='\n')
        writer.writeheader(); writer.writerows(rows); generated[DATA / (PREFIX + '-' + name + '.csv')] = stream.getvalue().encode()
    csv_file('paired-values', paired_values); csv_file('paired-summary', paired_rows)
    csv_file('official-values', official_values); csv_file('official-summary', official_rows)
    report = ['# Lazy per-Interpreter fallback globals trial', '',
        'Decision: **' + decision + '**. The paired callback probe is mixed and does not establish an overall gain. String-key dictionary lookup regresses to **0.865×** control speed (about 15.5% longer by this ratio); all seven pairs favor the control, with six settled ratios spanning 0.849×–0.885×. The intended native Python callback rows show descriptive 1.042× (Python-key dict) and 1.029× (Python-key hash), with 1.045× for the ordinary wrapper. These values are observations, not significance claims.', '',
        'The change defers construction of each Interpreter fallback unordered_map until an actual fallback store. The MSVC map normally allocates its sentinel and initial buckets on construction; ordinary module/dict-backed callbacks need neither. The candidate preserves global lookup order, per-Interpreter legacy fallback ownership and deletion-before-finalizer version changes. This source hypothesis does not directly explain the string-key lookup regression.', '',
        'The complete fixed regression gate passed all 11 cases with unchanged 21 paired repeats, 5 warmups and 10% tolerance. Correctness passed 381 core fixtures, 11 compatibility sections and 3 expected failures, seven focused paths, 9 configured CTests and two direct SQLite API checks. Existing strict CPython 3.14.7 reference fixtures remain pinned. Passing this gate does not erase the separate diagnostic regression.', '',
        'The existing C++ interpreter target covers lazy materialization, fallback isolation/persistence, module/dict/builtin precedence, nested reentry, cleanup reentry and host/fallback ownership during C++ exception unwind. Its actual working source hash is `' + source_hashes['tests/cpp/interpreter_lazy_globals_cases.h'] + '`; the compiled interpreter registration hash is `' + source_hashes['tests/cpp/interpreter_tests.cpp'] + '`. Exact seven changed source files and the 40-file validation inventory are preserved.', '',
        '| Callback diagnostic | Median pair control / candidate |', '|---|---:|']
    report.extend(f"| {r['path']} | {r['median_pair_control_over_candidate']:.3f}× |" for r in paired_rows)
    report += ['', 'Seven alternating pairs retain five raw samples per row/process, 16,384 operations per sample and all checksums: 560 timings. Each displayed ratio is the median of seven within-pair median-time ratios, **not** the ratio of pooled medians. Ratios above 1× favor the candidate. No confidence interval or significance test is supplied, and no samples/outliers are removed.', '',
        '## Selected official comparisons with saved CPython 3.14.7', '',
        'These two original fast-mode benchmarks completed with all 20 measured values per runtime. CPython was measured October 7; the candidate is fresh. The comparison is unpaired and cannot attribute changes to the lazy storage trial. CPython mean time / XLang3 mean time is shown with CPython = 1×. This checkpoint does **not** include a new full 97-definition pyperformance run.', '',
        '| Benchmark | Runtime | Mean ± sample SD (ms) | CV | Speed vs CPython |', '|---|---|---:|---:|---:|']
    report.extend(f"| {r['benchmark']} | {r['runtime']} | {r['mean_seconds']*1000:.6f} ± {r['sample_sd_seconds']*1000:.6f} | {r['cv_percent']:.2f}% | {r['speed_vs_cpython']:.3f}× |" for r in official_rows)
    report += ['', f'![Selected official speed](charts/{PREFIX}.svg)', '',
        f'[Official raw values](data/{PREFIX}-official-values.csv), [variation](data/{PREFIX}-official-summary.csv), [paired raw values](data/{PREFIX}-paired-values.csv), and [all pair ratios](data/{PREFIX}-paired-summary.csv) retain the full observations.', '',
        f'Exact source identities, terminal validation, fixed-gate/raw phase logs, official JSON, saved CPython receipts, callback logs and the Release build log are in the [owned manifest](data/{PREFIX}-manifest.json). Candidate executable/DLL identities are pinned without adding binaries to Git. The seven changed source files are archived and listed explicitly. Held/rejected exports include documents only in the owned staging list; source staging requires an accepted decision.', '',
        'Saved CPython executable/DLL, benchmark Python sources and compatibility hook are pinned by the reused receipts. Dependency METADATA does not prove every historical package/data byte. Existing SQLite GC-edge and conservative cursor-reentrancy limits remain in the raw validation receipt. No whole-suite or overall CPython win is claimed.', '']
    if warning_lines: report += ['Official warning lines (full logs archived):', ''] + ['> ' + line for line in warning_lines] + ['']
    generated[ROOT / 'doc/performance' / (PREFIX + '.md')] = '\n'.join(report).encode()
    maximum = max(1, *(r['speed_vs_cpython'] for r in official_rows))
    svg = ['<svg xmlns="http://www.w3.org/2000/svg" width="1000" height="310" viewBox="0 0 1000 310">', '<rect width="1000" height="310" fill="white"/>', '<text x="20" y="27" font-family="Arial" font-size="18">Selected official cases: CPython 3.14.7 = 1×</text>']
    for index, row in enumerate(official_rows):
        y = 55 + index * 54; width = 490 * row['speed_vs_cpython'] / maximum
        label = row['benchmark'] + (' CPython' if row['runtime'].startswith('cpython') else ' XLang3')
        svg += [f'<text x="20" y="{y+21}" font-family="Arial" font-size="14">{label}</text>', f'<rect x="310" y="{y}" width="{width:.3f}" height="29" fill="{"#64748b" if row["runtime"].startswith("cpython") else "#2563eb"}"/>', f'<text x="{820 if width > 465 else 322+width:.3f}" y="{y+21}" font-family="Arial" font-size="14">{row["speed_vs_cpython"]:.3f}×</text>']
    svg += ['<text x="20" y="292" font-family="Arial" font-size="12">Saved-reference fast runs; 20 values each. Unpaired; no lazy-change attribution or full-suite claim.</text>', '</svg>']
    generated[ROOT / 'doc/performance/charts' / (PREFIX + '.svg')] = '\n'.join(svg).encode()
    assert all(path.read_bytes() == item['raw'] for path, item in inputs.items()), 'Input drift during export'
    owned = set(SOURCES) if decision == 'accepted' else set()
    input_rows = []
    for path, item in inputs.items():
        relative = path.relative_to(ROOT).as_posix()
        target = ARCHIVE / relative if item['archive'] else None
        input_rows.append({'source': relative, 'sha256': item['sha256'], 'bytes': len(item['raw']),
            'archived': target.relative_to(ROOT).as_posix() if target else None})
        if target: generated[target] = item['raw']
    manifest_path = DATA / (PREFIX + '-manifest.json')
    manifest = {'status': 'terminal_lazy_globals_' + decision + '_trial_export', 'decision': decision, 'interpretation': 'mixed descriptive rows; consistent string-dict regression; no significant overall gain or fresh full97 claim',
        'validation_sha256': inputs[validation_path.resolve()]['sha256'], 'paired_sha256': inputs[paired_path.resolve()]['sha256'],
        'candidate_binary_sha256': validation['candidate_binary_sha256'], 'source_inventory_sha256': validation['source_inventory_sha256'],
        'changed_source_sha256': {name: source_hashes[name] for name in SOURCES}, 'fixture_counts': validation['fixture_counts'],
        'official_statistics': official_rows, 'paired_summary': paired_rows, 'inputs': input_rows,
        'owned_generated': [{'path': p.relative_to(ROOT).as_posix(), 'sha256': sha(raw), 'bytes': len(raw)} for p, raw in sorted(generated.items())]}
    generated[manifest_path] = (json.dumps(manifest, indent=2) + '\n').encode()
    for path, raw in generated.items():
        assert not path.exists(), 'Preserve earlier export: ' + str(path)
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open('xb') as stream: stream.write(raw)
        owned.add(path.relative_to(ROOT).as_posix())
    stage_path = SCRATCH / (PREFIX + '-owned-paths.nul')
    stage_manifest = SCRATCH / (PREFIX + '-owned-staging-manifest.json')
    assert not stage_path.exists() and not stage_manifest.exists()
    raw_paths = b''.join(name.encode('utf-8') + b'\0' for name in sorted(owned))
    stage_path.write_bytes(raw_paths)
    stage_manifest.write_bytes((json.dumps({'status': 'owned_paths_prepared_not_staged', 'decision': decision, 'engine_sources_included': decision == 'accepted', 'nul_sha256': sha(raw_paths),
        'nul_path': str(stage_path), 'count': len(owned), 'paths': [{'path': name, 'sha256': sha((ROOT / name).read_bytes())} for name in sorted(owned)]}, indent=2) + '\n').encode())
    print(json.dumps({'status': manifest['status'], 'owned_paths': len(owned), 'archive_inputs': sum(row['archived'] is not None for row in input_rows),
        'report': str(ROOT / 'doc/performance' / (PREFIX + '.md')), 'manifest': str(manifest_path),
        'nul_staging_list': str(stage_path), 'staging_manifest': str(stage_manifest), 'paired_values': len(paired_values), 'official_values': len(official_values)}))


if __name__ == '__main__': main()
