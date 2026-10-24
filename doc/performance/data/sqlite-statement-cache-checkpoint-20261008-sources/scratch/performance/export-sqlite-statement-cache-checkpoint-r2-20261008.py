"""Export saved native SQLite cache evidence only; never launch a workload or stage Git."""
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
PREFIX = 'sqlite-statement-cache-checkpoint-20261008'
ARCHIVE = DATA / (PREFIX + '-sources')
CP_STEM = 'pyperformance-cpython3147-live-eval-full-fast-20261007'
SOURCES = ['modules/sqlite/sqlite_handles.h', 'modules/sqlite/sqlite_handles.cpp',
    'modules/sqlite/sqlite_package.cpp', 'tests/cpp/interpreter_tests.cpp',
    'tests/run_fixtures.py', 'tests/run_fixtures.ps1', 'sdk/xlang3/abi/xmodule.h',
    'src/import/native_package_loader.cpp', 'tests/cpp/sqlite_statement_cache_cases.h',
    'tests/cpp/sqlite_statement_cache_sdk_probe.py', 'tests/fixtures/core/sqlite_statement_cache.py',
    'tests/fixtures/expected/sqlite_statement_cache.out']

def sha(raw): return hashlib.sha256(raw).hexdigest()


def file_sha(path):
    digest = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--decision', choices=('held', 'rejected', 'accepted'), default='held')
    parser.add_argument('--validation', type=Path, required=True)
    parser.add_argument('--official-pairs', type=Path, required=True)
    options = parser.parse_args()
    decision = options.decision
    assert sys.version_info[:3] == (3, 14, 7)
    assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
    inputs, external_inputs = {}, {}
    def keep(path, expected=None, archive=True):
        path = Path(path).resolve(strict=True)
        raw = path.read_bytes()
        assert expected is None or sha(raw) == expected, str(path)
        assert path not in inputs or inputs[path]['sha256'] == sha(raw)
        inputs[path] = {'raw': raw, 'sha256': sha(raw), 'archive': archive}
        return raw
    def read(path, expected=None): return json.loads(keep(path, expected).decode('utf-8-sig'))
    def pin_binary(path, expected):
        path = Path(path).resolve(strict=True)
        observed = file_sha(path)
        assert observed == expected, str(path)
        assert path not in inputs or inputs[path]['sha256'] == observed
        if path in inputs: return
        inputs[path] = {'raw': None, 'sha256': observed, 'bytes': path.stat().st_size, 'archive': False}
    def pin_identity(path, expected):
        path = Path(path).resolve(strict=True)
        if path.is_relative_to(ROOT):
            pin_binary(path, expected)
        else:
            assert file_sha(path) == expected, str(path)
            assert path not in external_inputs or external_inputs[path]['sha256'] == expected
            external_inputs[path] = {'sha256': expected, 'bytes': path.stat().st_size, 'raw': None, 'archive': None}
    validation_path = options.validation.resolve(strict=True)
    validation = read(validation_path)
    assert validation['status'] == 'validated' and validation['terminal'] and validation['hashes_unchanged']
    assert validation['correctness_passed'] and all(p['exit_code'] == 0 and p['passed'] for p in validation['phases'])
    assert validation['fixture_counts'] == {'core': 382, 'compatibility_sections': 11, 'expected_failures': 3}
    inventory = read(validation['source_inventory'], validation['source_inventory_sha256'])
    source_hashes = inventory['source_sha256']
    assert inventory['owned_sources'] == SOURCES
    assert len(source_hashes) == 44 and source_hashes == validation['source_sha256']
    for name, expected in source_hashes.items(): keep(ROOT / name, expected, archive=name in SOURCES)
    for name, expected in validation['binaries_sha256'].items(): pin_binary(ROOT / name, expected)
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
    paired_path = DATA / 'sqlite-statement-cache-r4-callback-paired-20261008.json'
    paired = read(paired_path)
    assert paired['terminal'] and paired['hashes_unchanged'] and paired['status'] == 'terminal_diagnostic_only'
    assert paired['source_hashes_unchanged']
    assert paired['pairs'] == 7 and paired['samples_per_pair'] == 5 and len(paired['raw']) == 14
    assert paired['engine_source_sha256'] == source_hashes[SOURCES[0]]
    for path, expected in paired['binaries_sha256'].items(): pin_binary(path, expected)
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
    before_root = SCRATCH / 'sqlite-statement-cache-r4-actual-source-before-20261008'
    before = read(before_root / 'manifest.json', inventory['accepted_raw_before_manifest_sha256'])
    assert set(before['added_paths']) == {name for name in SOURCES if name not in before['source_sha256']}
    for name, expected in before['source_sha256'].items():
        keep(before_root / name, expected, archive=name in SOURCES)
    for revision, expected_exit in [('r3', 1), ('r4', 0)]:
        reference_path = DATA / ('sqlite-statement-cache-' + revision + '-cpython3147-reference-20261008.json')
        reference = read(reference_path)
        assert reference['status'] == 'terminal' and reference['exit_code'] == expected_exit
        assert reference['cpython_version'].startswith('3.14.7 ')
        keep(reference['source'], reference['source_sha256'])
        for kind in ('stdout', 'stderr'):
            keep(reference_path.with_name(reference_path.stem + '.' + kind + '.log'), reference[kind + '_sha256'])
        if revision == 'r4':
            assert reference['output_matches_expected'] and sha(inputs[reference_path.resolve()]['raw']) == inventory['cpython_reference_sha256']
    first_idle = read(DATA / 'sqlite-statement-cache-validation-20261008.json')
    assert first_idle['terminal'] and first_idle['status'].startswith('failed') and not first_idle['phases']
    for relative in ('scratch/performance/sqlite-statement-cache-r4-proposal-20261008.patch',
        'scratch/performance/sqlite-statement-cache-r4-proposal-20261008-provenance.json',
        'scratch/performance/sqlite-statement-cache-r3-proposal-20261008.patch',
        'scratch/performance/sqlite-statement-cache-r3-proposal-20261008-provenance.json',
        'scratch/performance/measure-sqlite-statement-cache-r4-callback-paired-20261008.py',
        'scratch/performance/measure-sqlite-statement-cache-original-paired-20261008.py',
        'scratch/performance/measure-sqlite-statement-cache-original-paired-20261008-provenance.json',
        'scratch/performance/python-hash-callback-cost-probe-20261008.py',
        'scratch/performance/validate-sqlite-statement-cache-r2-20261008.py',
        'scratch/performance/apply-sqlite-statement-cache-r4-20261008.py',
        'doc/performance/data/sqlite-statement-cache-r4-final-source-20261008.json',
        'doc/performance/data/build-sqlite-statement-cache-r4-Release-20261008.log',
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
    original_pairs_path = options.official_pairs.resolve(strict=True)
    original_pairs = read(original_pairs_path)
    assert original_pairs['terminal'] and original_pairs['hashes_unchanged']
    assert original_pairs['status'] == 'validated_paired_original_official'
    assert (original_pairs['pair_count'], original_pairs['samples_per_run']) == (3, 20)
    assert original_pairs['benchmark'] == 'sqlite_synth' and original_pairs['mode'] == 'fast'
    assert original_pairs['timed_runtimes'] == ['control', 'candidate'] and original_pairs['no_cpython_rerun']
    assert Path(original_pairs['validation']).resolve() == validation_path
    assert original_pairs['validation_sha256'] == inputs[validation_path]['sha256']
    assert original_pairs['source_inventory_sha256'] == validation['source_inventory_sha256']
    assert original_pairs['source_sha256'] == source_hashes
    assert original_pairs['fixed_gate_sha256'] == validation['fixed_gate']['sha256']
    assert original_pairs['tracked_sha256_before'] == original_pairs['tracked_sha256_after']
    assert original_pairs['binaries_sha256_start'] == original_pairs['binaries_sha256_end']
    assert all(not row['busy'] for row in original_pairs['idle_guards'])
    assert original_pairs['manager_version'].startswith('3.14.7 ')
    assert Path(original_pairs['manager_executable']).resolve() == Path(sys.executable).resolve()
    assert original_pairs['compatibility_hook_sha256'] == validation['compatibility_hook_sha256']
    assert original_pairs['manager_configuration_provenance_sha256'] == inputs[(DATA / (CP_STEM + '-provenance.json')).resolve()]['sha256']
    assert original_pairs['original_source_sha256'] == 'dcba7a6889e66f08480f4332860208d624678fc42a03be5c8f3b6f1f894231fd'
    for path, expected in original_pairs['tracked_sha256_before'].items(): pin_identity(path, expected)
    original_source = Path(original_pairs['original_source']).resolve(strict=True)
    original_raw = original_source.read_bytes()
    assert sha(original_raw) == original_pairs['original_source_sha256']
    external_inputs[original_source] = {'sha256': sha(original_raw), 'bytes': len(original_raw),
        'raw': original_raw, 'archive': ARCHIVE / 'external/original_sqlite_synth/run_benchmark.py'}
    control_manifest = read(original_pairs['control_manifest'], original_pairs['control_manifest_sha256'])
    assert control_manifest['accepted'] and len(control_manifest['files_sha256']) == 140
    assert original_pairs['binaries_sha256_start']['control'] == control_manifest['files_sha256']
    assert set(original_pairs['binaries_sha256_start']) == {'control', 'candidate'}
    for role, executable in original_pairs['runtime_executables'].items():
        assert role in ('control', 'candidate')
        executable = Path(executable).resolve(strict=True)
        rows = original_pairs['binaries_sha256_start'][role]
        assert len(rows) == 140 and set(rows) == set(control_manifest['files_sha256'])
        for relative, expected in rows.items(): pin_binary(executable.parent / relative, expected)
        if role == 'candidate':
            assert file_sha(executable) == validation['candidate_binary_sha256']['exe']
            for relative in ('xlang3.exe', 'xlang3_runtime.dll', 'modules\\xlang_sqlite3.x3pkg.dll'):
                path = executable.parent / relative
                assert rows[relative] == validation['binaries_sha256'][path.relative_to(ROOT).as_posix()]
    original_values, original_run_rows, original_pair_rows = [], [], []
    original_runs = original_pairs['runs']
    assert len(original_runs) == 6
    assert {(row['pair'], row['role']) for row in original_runs} == {(pair, role) for pair in (1, 2, 3) for role in ('control', 'candidate')}
    recomputed = {}
    for item in original_runs:
        assert item['exit_code'] == 0
        order = ['control', 'candidate'] if item['pair'] % 2 else ['candidate', 'control']
        assert item['order'] == order
        document = read(DATA / item['output'], item['output_sha256'])
        raw_log = keep(DATA / item['log'], item['log_sha256'])
        measured = values(document, 'sqlite_synth')
        assert len(document['benchmarks']) == 1 and measured == item['values_seconds']
        mean, sd = statistics.fmean(measured), statistics.stdev(measured)
        stats = {'mean_seconds': mean, 'sd_seconds': sd, 'cv_percent': 100 * sd / mean,
            'median_seconds': statistics.median(measured), 'min_seconds': min(measured), 'max_seconds': max(measured)}
        assert set(item['stats']) == set(stats)
        assert all(math.isclose(item['stats'][name], value, rel_tol=1e-13) for name, value in stats.items())
        warning = 'WARNING: the benchmark result may be unstable' in raw_log.decode('utf-8', errors='replace')
        assert item['instability_warning'] == warning
        recomputed[(item['pair'], item['role'])] = stats
        original_run_rows.append({'pair': item['pair'], 'runtime': item['role'], 'order': ';'.join(order),
            'values': 20, **stats, 'instability_warning': warning})
        for sample, seconds in enumerate(measured):
            original_values.append({'benchmark': 'sqlite_synth', 'pair': item['pair'], 'runtime': item['role'],
                'order': ';'.join(order), 'sample': sample, 'seconds': seconds})
    for pair in (1, 2, 3):
        control, candidate = (recomputed[(pair, role)]['mean_seconds'] for role in ('control', 'candidate'))
        original_pair_rows.append({'pair': pair, 'order': ['control', 'candidate'] if pair % 2 else ['candidate', 'control'],
            'control_mean_seconds': control, 'candidate_mean_seconds': candidate, 'control_over_candidate_speed': control / candidate})
    assert original_pairs['paired_rows'] == original_pair_rows
    pair_ratios = [row['control_over_candidate_speed'] for row in original_pair_rows]
    original_summary = {'median_pair_speed': statistics.median(pair_ratios), 'pair_ratios': pair_ratios,
        'candidate_faster_pairs': sum(value > 1 for value in pair_ratios), 'pairs': 3,
        'raw_values_per_runtime': 60, 'values_trimmed': 0, 'outliers_removed': 0}
    assert len(original_values) == 120 and original_pairs['summary'] == original_summary
    for relative, expected in original_pairs['partial_evidence_sha256'].items(): keep(DATA / relative, expected)
    generated = {}
    def csv_file(name, rows):
        stream = io.StringIO(newline=''); writer = csv.DictWriter(stream, fieldnames=list(rows[0]), lineterminator='\n')
        writer.writeheader(); writer.writerows(rows); generated[DATA / (PREFIX + '-' + name + '.csv')] = stream.getvalue().encode()
    csv_file('paired-values', paired_values); csv_file('paired-summary', paired_rows)
    csv_file('official-values', official_values); csv_file('official-summary', official_rows)
    csv_file('original-sqlite-paired-values', original_values)
    csv_file('original-sqlite-paired-run-statistics', original_run_rows)
    csv_file('original-sqlite-paired-summary', [{**row, 'order': ';'.join(row['order'])} for row in original_pair_rows])
    report = ['# Native SQLite prepared-statement cache checkpoint', '',
        'Decision: **' + decision + '**. Measurements below preserve the original official workload and all paired observations. The callback diagnostic is a negative-control screen and does not establish a whole-suite speed gain.', '',
        'The own native `_sqlite3` module now keeps a configurable 128-entry connection LRU. It leases a prepared VM before parameter callbacks, prepares a separate VM for overlapping identical SQL, and makes a successful completed statement reusable only after reset and cleared bindings. Eviction, invalidation and close detach ownership before callback-capable destruction. Comments beside these paths explain the fast reuse and lifetime guards; Python aggregate algorithms remain Python.', '',
        'The appended optional package-host `value_index` callback preserves ABI version 24 and existing prefixes. The native cache-size argument uses type-level index lookup, owned result transport and original pending exceptions. It does not read the mutable public `operator.index` function or ordinary instance overrides. Existing numeric-subclass and omitted-parameter-count limitations remain explicit.', '',
        'Validation passed 382 core fixtures, 11 compatibility sections, 3 expected failures, 8 focused paths, 9 configured CTests and 2 direct SQLite API checks. The complete fixed 11-case gate retains 21 paired repeats, 5 warmups and 10% tolerance. Actual native C++ tests establish eight identical fresh-cursor INSERTs use one explicit prepare; disabled caching uses one per execute, capacity 2 obeys LRU, and nested/overlapping statements preserve independent leases and factory cleanup.', '',
        'The R3 CPython 3.14.7 fixture failed because calling Cursor.close after Connection.close also raises ProgrammingError. R4 replaces only that invalid cleanup assumption with deletion/collection, while retaining the strict closed-fetch error and seven expected groups. Both original CP receipts and raw logs are archived. The first candidate controller refused a transient external CTest before any workload phase; that failed preflight receipt is preserved. Actual compiled source bytes, 12 owned changes, 44-source inventory and pre-trial raw snapshots are pinned separately from normalized scratch proposals.', '',
        '## Original official SQLite pairs', '',
        f"The unchanged original `sqlite_synth` completed three alternating serial pairs against the preserved accepted Release. The median within-pair mean-time ratio is **{original_summary['median_pair_speed']:.3f}×**; the candidate is faster in {original_summary['candidate_faster_pairs']} of three pairs. Each run keeps all 20 measured values, for 60 values per runtime. CPython 3.14.7 manages these runs and is not a timed runtime in this paired comparison. No values or outliers are removed.", '',
        '| Pair | Order | Accepted control mean (ms) | Candidate mean (ms) | Control / candidate |', '|---|---|---:|---:|---:|']
    report.extend(f"| {row['pair']} | {' → '.join(row['order'])} | {row['control_mean_seconds']*1000:.6f} | {row['candidate_mean_seconds']*1000:.6f} | {row['control_over_candidate_speed']:.3f}× |" for row in original_pair_rows)
    report += ['', f'[All 120 paired SQLite values](data/{PREFIX}-original-sqlite-paired-values.csv), [six run statistics and warnings](data/{PREFIX}-original-sqlite-paired-run-statistics.csv), and [three pair ratios](data/{PREFIX}-original-sqlite-paired-summary.csv) retain the observations and alternating order. The mean ratio is calculated separately within each pair; the reported result is the median of those three ratios.', '',
        '## Callback negative control', '',
        '| Callback negative control | Median pair control / candidate |', '|---|---:|']
    report.extend(f"| {r['path']} | {r['median_pair_control_over_candidate']:.3f}× |" for r in paired_rows)
    string_dict_row = next(row for row in paired_rows if row['path'] == 'string_dict_get')
    string_dict_ratio = string_dict_row['median_pair_control_over_candidate']
    report += ['', 'Seven alternating pairs retain five raw samples per row/process, 16,384 operations per sample and all checksums: 560 timings. Each displayed ratio is the median of seven within-pair median-time ratios, **not** the ratio of pooled medians. Ratios above 1× favor the candidate. No confidence interval or significance test is supplied, and no samples/outliers are removed.', '',
        f"The string-key dictionary control measures **{string_dict_ratio:.3f}×**, corresponding to {(1/string_dict_ratio-1)*100:.2f}% more time per sample. All seven within-pair ratios are below 1× across both execution orders. The slowdown's cause is unresolved; these observations do not establish code layout or another particular cause. A passed fixed gate does not erase this additional negative-control evidence.", '',
        '## Selected official comparisons with saved CPython 3.14.7', '',
        'These two original fast-mode benchmarks completed with all 20 measured values per runtime. CPython was measured October 7; the candidate is fresh. The comparison is unpaired and cannot attribute changes to the cache implementation. CPython mean time / XLang3 mean time is shown with CPython = 1×. This checkpoint does **not** include a new full 97-definition pyperformance run.', '',
        '| Benchmark | Runtime | Mean ± sample SD (ms) | CV | Speed vs CPython |', '|---|---|---:|---:|---:|']
    report.extend(f"| {r['benchmark']} | {r['runtime']} | {r['mean_seconds']*1000:.6f} ± {r['sample_sd_seconds']*1000:.6f} | {r['cv_percent']:.2f}% | {r['speed_vs_cpython']:.3f}× |" for r in official_rows)
    report += ['', f'![Selected official speed](charts/{PREFIX}.svg)', '',
        f'[Official raw values](data/{PREFIX}-official-values.csv), [variation](data/{PREFIX}-official-summary.csv), [paired raw values](data/{PREFIX}-paired-values.csv), and [all pair ratios](data/{PREFIX}-paired-summary.csv) retain the full observations.', '',
        f'Exact source identities, terminal validation, fixed-gate/raw phase logs, all six paired official JSON/log files, saved CPython receipts, callback logs and the Release build log are in the [owned manifest](data/{PREFIX}-manifest.json). The original SQLite benchmark source is archived byte for byte. Candidate executable/DLL identities are pinned without adding binaries to Git. The twelve changed source files are archived and listed explicitly. Held/rejected exports include documents only in the owned staging list; source staging requires an accepted decision.', '',
        'Saved CPython executable/DLL, benchmark Python sources and compatibility hook are pinned by the reused receipts. Dependency METADATA does not prove every historical package/data byte. Existing SQLite GC-edge and conservative cursor-reentrancy limits remain in the raw validation receipt. No whole-suite or overall CPython win is claimed.', '']
    if warning_lines: report += ['Official warning lines (full logs archived):', ''] + ['> ' + line for line in warning_lines] + ['']
    generated[ROOT / 'doc/performance' / (PREFIX + '.md')] = '\n'.join(report).encode()
    maximum = max(1, *(r['speed_vs_cpython'] for r in official_rows))
    svg = ['<svg xmlns="http://www.w3.org/2000/svg" width="1000" height="310" viewBox="0 0 1000 310">', '<rect width="1000" height="310" fill="white"/>', '<text x="20" y="27" font-family="Arial" font-size="18">Selected official cases: CPython 3.14.7 = 1×</text>']
    for index, row in enumerate(official_rows):
        y = 55 + index * 54; width = 490 * row['speed_vs_cpython'] / maximum
        label = row['benchmark'] + (' CPython' if row['runtime'].startswith('cpython') else ' XLang3')
        svg += [f'<text x="20" y="{y+21}" font-family="Arial" font-size="14">{label}</text>', f'<rect x="310" y="{y}" width="{width:.3f}" height="29" fill="{"#64748b" if row["runtime"].startswith("cpython") else "#2563eb"}"/>', f'<text x="{820 if width > 465 else 322+width:.3f}" y="{y+21}" font-family="Arial" font-size="14">{row["speed_vs_cpython"]:.3f}×</text>']
    svg += ['<text x="20" y="292" font-family="Arial" font-size="12">Saved-reference fast runs; 20 values each. Unpaired; no cache-change attribution or full-suite claim.</text>', '</svg>']
    generated[ROOT / 'doc/performance/charts' / (PREFIX + '.svg')] = '\n'.join(svg).encode()
    assert all(file_sha(path) == item['sha256'] for path, item in inputs.items()), 'Input drift during export'
    assert all(file_sha(path) == item['sha256'] for path, item in external_inputs.items()), 'External identity drift during export'
    owned = set(SOURCES) if decision == 'accepted' else set()
    input_rows = []
    for path, item in inputs.items():
        relative = path.relative_to(ROOT).as_posix()
        target = ARCHIVE / relative if item['archive'] else None
        input_rows.append({'source': relative, 'sha256': item['sha256'], 'bytes': item.get('bytes', len(item['raw']) if item['raw'] is not None else 0),
            'archived': target.relative_to(ROOT).as_posix() if target else None})
        if target: generated[target] = item['raw']
    external_rows = []
    for path, item in external_inputs.items():
        target = item['archive']
        external_rows.append({'source': str(path), 'sha256': item['sha256'], 'bytes': item['bytes'],
            'archived': target.relative_to(ROOT).as_posix() if target else None})
        if target: generated[target] = item['raw']
    manifest_path = DATA / (PREFIX + '-manifest.json')
    manifest = {'status': 'terminal_sqlite_cache_' + decision + '_trial_export', 'decision': decision, 'interpretation': 'Original official paired SQLite evidence; callback negative controls; no fresh full97 claim',
        'validation_sha256': inputs[validation_path.resolve()]['sha256'], 'paired_sha256': inputs[paired_path.resolve()]['sha256'],
        'original_official_paired_sha256': inputs[original_pairs_path]['sha256'],
        'original_official_paired_summary': original_summary,
        'candidate_binary_sha256': validation['candidate_binary_sha256'], 'source_inventory_sha256': validation['source_inventory_sha256'],
        'changed_source_sha256': {name: source_hashes[name] for name in SOURCES}, 'fixture_counts': validation['fixture_counts'],
        'official_statistics': official_rows, 'paired_summary': paired_rows, 'inputs': input_rows, 'external_inputs': external_rows,
        'owned_generated': [{'path': p.relative_to(ROOT).as_posix(), 'sha256': sha(raw), 'bytes': len(raw)} for p, raw in sorted(generated.items())]}
    generated[manifest_path] = (json.dumps(manifest, indent=2) + '\n').encode()
    stage_path = SCRATCH / (PREFIX + '-owned-paths.nul')
    stage_manifest = SCRATCH / (PREFIX + '-owned-staging-manifest.json')
    assert not stage_path.exists() and not stage_manifest.exists()
    assert all(not path.exists() for path in generated), 'Preserve earlier export'
    for path, raw in generated.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open('xb') as stream: stream.write(raw)
        owned.add(path.relative_to(ROOT).as_posix())
    raw_paths = b''.join(name.encode('utf-8') + b'\0' for name in sorted(owned))
    stage_path.write_bytes(raw_paths)
    stage_manifest.write_bytes((json.dumps({'status': 'owned_paths_prepared_not_staged', 'decision': decision, 'engine_sources_included': decision == 'accepted', 'nul_sha256': sha(raw_paths),
        'nul_path': str(stage_path), 'count': len(owned), 'paths': [{'path': name, 'sha256': sha((ROOT / name).read_bytes())} for name in sorted(owned)]}, indent=2) + '\n').encode())
    print(json.dumps({'status': manifest['status'], 'owned_paths': len(owned), 'archive_inputs': sum(row['archived'] is not None for row in input_rows),
        'report': str(ROOT / 'doc/performance' / (PREFIX + '.md')), 'manifest': str(manifest_path),
        'nul_staging_list': str(stage_path), 'staging_manifest': str(stage_manifest), 'paired_values': len(paired_values), 'official_values': len(official_values),
        'original_official_paired_values': len(original_values), 'original_official_pair_ratios': pair_ratios}))


if __name__ == '__main__': main()
