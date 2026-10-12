"""Build a new full comparison after both fresh jobs are authoritative terminal."""
import csv
import datetime
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import statistics
import subprocess
import sys

assert sys.version_info[:3] == (3, 14, 7)
root = Path.cwd()
data = root / 'doc/performance/data'
prefixes = {'xlang3': 'pyperformance-xlang3-live-eval-full-fast-20261007',
            'cpython3147': 'pyperformance-cpython3147-live-eval-full-fast-20261007'}
prefix = 'pyperformance-xlang3-live-eval-vs-cpython3147-full-fast-20261007'
digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
records, inputs = {}, {}
generator = root / 'benchmarks/diagnostics/summarize_pyperformance_comparison.py'
spec = importlib.util.spec_from_file_location('summary', generator)
summary = importlib.util.module_from_spec(spec)
spec.loader.exec_module(summary)
for kind, name in prefixes.items():
    paths = [data / (name + suffix) for suffix in ('.json', '.log', '-provenance.json')]
    assert all(path.is_file() for path in paths), kind
    record = json.loads(paths[-1].read_text(encoding='utf-8'))
    assert record['status'] in ('finished', 'finished_with_benchmark_failures')
    assert record['sha256_start'] == record['sha256_end']
    assert record['expected_definitions'] == record['attempted_definitions'] == 97
    assert record['benchmarks'] == 'all' and record['mode'] == 'fast'
    assert len(summary.case_sections(paths[1].read_text(encoding='utf-8'))) == 97
    records[kind] = record
    inputs[kind] = {path.name: digest(path) for path in paths}
    partial = data / (name + '-partial')
    if partial.is_dir():
        inputs[kind]['partial_evidence_sha256'] = {p.relative_to(data).as_posix(): digest(p) for p in sorted(partial.glob('*.json'))}
        for path in partial.glob('*.provenance.json'):
            item = json.loads(path.read_text(encoding='utf-8'))
            assert item['definition_status'] == 'failed'
            assert digest(partial / item['partial_output']) == item['sha256']
xrecord, crecord = records['xlang3'], records['cpython3147']
assert crecord['runtime_version'] == '3.14.7'
assert Path(crecord['runtime_executable']).resolve() == Path(r'C:\Python\Python314\python.exe').resolve()
assert datetime.datetime.fromisoformat(xrecord['completed_utc']) <= datetime.datetime.fromisoformat(crecord['started_utc'])
for key in ('benchmark_python_sources', 'dependency_metadata_sha256', 'compatibility_hook_sha256', 'runner_sha256'):
    assert xrecord[key] == crecord[key], key
checkpoint = json.loads((data / 'eval-live-namespace-checkpoint-20261007.json').read_text(encoding='utf-8'))
assert all(xrecord['sha256_end'][kind] == checkpoint['binary_sha256']['xlang3'][kind] for kind in ('exe', 'dll'))
outputs = [data / (prefix + '-all-97-status.csv'), data / (prefix + '-subtests.csv'),
           data.parent / (prefix + '.md'), data.parent / (prefix + '.svg'),
           data / (prefix + '-sample-variation.csv'),
           data / (prefix + '-failed-partial-subtests.csv'),
           data / (prefix + '-comparison-provenance.json')]
assert not any(path.exists() for path in outputs)
gc_evidence_path = data / 'gc-traversal-coverage-exclusion-20261007.json'
assert not gc_evidence_path.exists()
gc_sources = ['src/runtime/modules/system/gc_module.cpp',
              'src/runtime/modules/system/weakref_module.cpp', 'src/runtime/object_model.cpp']
gc_source_hashes = {}
for source in gc_sources:
    raw = (root / source).read_bytes()
    committed = subprocess.check_output(['git', 'show', xrecord['source_base_commit'] + ':' + source])
    assert raw.replace(b'\r\n', b'\n') == committed.replace(b'\r\n', b'\n'), source
    gc_source_hashes[source] = {'raw_sha256': digest(root / source),
                               'canonical_lf_sha256': hashlib.sha256(raw.replace(b'\r\n', b'\n')).hexdigest()}
gc_evidence = {'classification': 'Speed score withheld for incomplete collection work; worker status and raw timing retained',
               'invalid_subtest': 'gc_traversal', 'source_base_commit': xrecord['source_base_commit'],
               'source_sha256': gc_source_hashes,
               'source_findings': ['gc.collect delegates to weakref_collect_cycles',
                                  'Collector seeds local classes, weakref targets and native-payload instances, not the generic tracked heap',
                                  'Ordinary benchmark Node instances and the retained list graph have no weakref/native-payload candidate seed',
                                  'Native GC registry insertion requires native traversal/reference and clear hooks'],
               'observed_gc_collect_definition_status': xrecord['failed_definitions'].get('gc_collect', 'not failed'),
               'actual_returned_collection_count': 'Not recorded by the assertion traceback; focused observation still pending',
               'xlang_full_json_sha256': inputs['xlang3'][prefixes['xlang3'] + '.json'],
               'scope': 'Source coverage evidence plus official collection failure; no invented count or completed-work equivalence claim'}
gc_evidence_path.write_text(json.dumps(gc_evidence, indent=2) + '\n', encoding='utf-8')
inputs['gc_workload_exclusion'] = {gc_evidence_path.name: digest(gc_evidence_path)}
command = [sys.executable, str(generator), '--xlang-json', str(data / (prefixes['xlang3'] + '.json')),
           '--cpython-json', str(data / (prefixes['cpython3147'] + '.json')),
           '--xlang-log', str(data / (prefixes['xlang3'] + '.log')),
           '--cpython-log', str(data / (prefixes['cpython3147'] + '.log')),
           '--canonical-status', str(data / 'pyperformance-xlang3-super-method-call-vs-cpython3147-full-fast-20261007-all-97-status.csv'),
           '--output-dir', str(data), '--prefix', prefix,
           '--release-exe-sha256', xrecord['sha256_end']['exe'], '--release-dll-sha256', xrecord['sha256_end']['dll'],
           '--case-timeout', '300', '--case-timeout-override', 'networkx*=600s', '--xlang-stdlib', r'C:\Python\Python314\Lib',
           '--invalid-subtest', 'gc_traversal=Collector omits generic tracked list/instance graph discovery; cycle-collection case also fails. Completed status and raw timing retained, speed score withheld.',
           '--correctness-evidence', str(gc_evidence_path)]
subprocess.run(command, check=True)
with outputs[1].open(encoding='utf-8-sig', newline='') as stream:
    scored_rows = [row for row in csv.DictReader(stream) if row['CPython / XLang3 speedup']]
variation_rows = []
warning_cases = {}
for kind, name in prefixes.items():
    benchmarks = summary.benchmark_map(json.loads((data / (name + '.json')).read_text(encoding='utf-8')))
    sections = summary.case_sections((data / (name + '.log')).read_text(encoding='utf-8'))
    warning_cases[kind] = sorted({row['benchmark'] for row in scored_rows
                                 if 'WARNING: the benchmark result may be unstable' in sections[row['benchmark']]})
    for row in scored_rows:
        samples = summary.values(benchmarks[row['subtest']])
        assert len(samples) >= 2
        mean_value = statistics.fmean(samples)
        deviation = statistics.stdev(samples)
        variation_rows.append({'runtime': kind, 'benchmark': row['benchmark'], 'subtest': row['subtest'],
                               'sample_count': len(samples), 'mean_seconds': mean_value,
                               'stddev_seconds': deviation, 'stddev_over_mean': deviation / mean_value,
                               'min_seconds': min(samples), 'max_seconds': max(samples),
                               'definition_has_pyperf_instability_warning': row['benchmark'] in warning_cases[kind]})
with outputs[4].open('w', encoding='utf-8', newline='') as stream:
    writer = csv.DictWriter(stream, fieldnames=list(variation_rows[0]))
    writer.writeheader()
    writer.writerows(variation_rows)
partial_rows = []
partial_fields = ['runtime', 'benchmark', 'partial_file', 'sha256', 'json_valid', 'subtest',
                  'sample_count', 'mean_seconds', 'stddev_seconds', 'not_scored_reason']
for kind, name in prefixes.items():
    directory = data / (name + '-partial')
    for path in sorted(directory.glob('*.provenance.json')):
        item = json.loads(path.read_text(encoding='utf-8'))
        raw_path = directory / item['partial_output']
        assert item['definition_status'] == 'failed' and digest(raw_path) == item['sha256']
        row_base = {'runtime': kind, 'benchmark': item['definition'],
                    'partial_file': raw_path.relative_to(data).as_posix(), 'sha256': item['sha256'],
                    'json_valid': item['json_valid'], 'subtest': '', 'sample_count': 0,
                    'mean_seconds': '', 'stddev_seconds': '', 'not_scored_reason': 'definition_failed'}
        timed_items = {}
        if item['json_valid'] and not item.get('schema_error'):
            try:
                timed_items = summary.benchmark_map(json.loads(raw_path.read_text(encoding='utf-8')))
            except (ValueError, AttributeError, TypeError):
                pass
        observed = False
        for subtest, benchmark in timed_items.items():
            samples = summary.values(benchmark)
            if not samples:
                continue
            observed = True
            partial_rows.append({**row_base, 'subtest': subtest, 'sample_count': len(samples),
                                 'mean_seconds': statistics.fmean(samples),
                                 'stddev_seconds': statistics.stdev(samples) if len(samples) > 1 else ''})
        if not observed:
            partial_rows.append(row_base)
with outputs[5].open('w', encoding='utf-8', newline='') as stream:
    writer = csv.DictWriter(stream, fieldnames=partial_fields)
    writer.writeheader()
    writer.writerows(partial_rows)
report = outputs[2]
text = report.read_text(encoding='utf-8')
reference_result = ('CPython 3.14.7 completed all **97** definitions with **0** failures, '
                    'recording **' + str(crecord['recorded_subtests']) + '** subtests.')
text = text.replace('\n\nOf **', '\n\n' + reference_result + '\n\nOf **', 1)
scored_geomean = math.exp(statistics.fmean(math.log(float(row['CPython / XLang3 speedup'])) for row in scored_rows))
text = text.replace('Fast-mode samples carry stability warnings and are directional evidence.',
                    'Across this scored subset, XLang3 takes **' + format(1 / scored_geomean, '.2f')
                    + '× CPython\'s time** by geometric mean. This excludes failed definitions and '
                    'the withheld GC traversal score; it is not a successful whole-suite result. '
                    'Fast-mode samples carry stability warnings and are directional evidence.', 1)
text = text.replace('speed score withheld..', 'speed score withheld.')
text += ('\n## Sample variation\n\n'
         'The [sample-variation CSV](data/' + outputs[4].name + ') records sample counts, means, '
         'sample standard deviations, relative deviations and ranges for the scored subtests of both runtimes. '
         'Pyperf instability warnings occurred in ' + str(len(warning_cases['xlang3'])) + ' XLang3 definitions '
         'and ' + str(len(warning_cases['cpython3147'])) + ' CPython definitions among this scored set. '
         'Warnings are recorded at definition level; a definition may emit multiple subtests. '
         'The CSV excludes failed definitions and their partial values. '
         'These are descriptive statistics, not independent samples proving significance or a confidence interval for speed ratios.\n')
text += ('\n## Unscored partial evidence\n\n'
         'The [failed-definition partial table](data/' + outputs[5].name + ') preserves '
         'available subtest sample counts and descriptive timings from failed definitions, '
         'with original file paths and SHA-256 hashes. It includes no CPython speed ratios '
         'and does not contribute to charts, win counts, or either geometric mean. '
         'Different saved files are separate snapshots; their samples are not pooled. '
         'Invalid or untimed partial files retain an index row without an invented timing.\n')
text += ('\n## Current workload evidence\n\n'
         'Genshi XML now contains the actual 1,000 rows and 10,000 cells, with exact output hashes matching CPython; '
         'its current timing is included. The earlier unexpanded XML result stays excluded in its historical report. '
         'This is not a claim that every completed benchmark has independent workload-equivalence validation. '
         'Nominal wins remain subject to semantic validation and noisy fast-mode timing.\n\n'
         '[Validated runtime checkpoint and exact Genshi outputs](eval-live-namespace-checkpoint-20261007.md).\n')
if 'gc_collect' in xrecord['failed_definitions']:
    text += ('\nThe native cycle-collection case `gc_collect` failed its assertion that at least '
             '2,100 unreachable cycle nodes are collected. `gc_traversal` retains its nested '
             'containers and checks that collection returns zero. Its nominal traversal timing '
             'does not establish cycle reclamation or complete GC compatibility. '
             'Source inspection confirms `gc.collect()` delegates to a collector seeded by '
             'local class, weakref-target and native-payload candidates, without enumerating '
             'the generic tracked list/instance heap. The benchmark list graph has no such '
             'candidate seed. Its completed status and raw timing remain recorded, but its '
             'speed ratio, chart bar, win count and aggregate contribution are withheld.\n')
text += ('\nSource inspection also identified that the current class-body lowerer omits '
         '`for` statements, matching the dictionary-population failures in Docutils and '
         'Pygments imported by Mako. Those failed definitions are excluded from scores. '
         'Other completed definitions have not all been independently checked for equivalent '
         'class-body execution; completion and nominal timings are not blanket semantic certification. '
         'The validated Genshi output counts/hashes are separate stronger evidence for that workload.\n')
xml_backends = []
for kind, name in prefixes.items():
    sources = [(data / (name + '.json'), 'full record')]
    directory = data / (name + '-partial')
    for path in sorted(directory.glob('*.provenance.json')):
        item = json.loads(path.read_text(encoding='utf-8'))
        if item['definition'] == 'xml_etree' and item['json_valid'] and not item.get('schema_error'):
            sources.append((directory / item['partial_output'], 'failed partial; unscored'))
    for path, evidence_status in sources:
        payload = json.loads(path.read_text(encoding='utf-8'))
        for benchmark in payload.get('benchmarks', []):
            metadata = {**payload.get('metadata', {}), **benchmark.get('metadata', {})}
            subtest = metadata.get('name', '')
            if subtest.startswith('xml_etree_'):
                xml_backends.append({'runtime': kind, 'subtest': subtest,
                                     'elementtree_module': metadata.get('elementtree_module', 'not recorded'),
                                     'evidence_status': evidence_status,
                                     'source': path.relative_to(data).as_posix(), 'sha256': digest(path)})
if xml_backends:
    text += ('\n## ElementTree backend metadata\n\n'
             'The official benchmark distinguishes accelerator and pure-Python subtest names. '
             'The recorded backends below remain attached to their original names; different '
             'variants are not silently renamed or scored as exact-name matches. Failed partial '
             'records remain unscored.\n\n'
             '| Runtime | Subtest | Recorded backend | Evidence |\n'
             '|---|---|---|---|\n')
    seen_backend_rows = set()
    for item in xml_backends:
        identity = tuple(item[key] for key in ('runtime', 'subtest', 'elementtree_module', 'evidence_status'))
        if identity in seen_backend_rows:
            continue
        seen_backend_rows.add(identity)
        text += '| ' + ' | '.join(str(value).replace('|', '\\|') for value in identity) + ' |\n'
report.write_text(text, encoding='utf-8', newline='\n')
proof = {'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
         'scope': 'New complete all-97 attempts for both runtimes; failed definitions excluded from scores',
         'input_sha256': inputs, 'generator_sha256': digest(generator), 'generator_command': command,
         'report_controller_sha256': digest(Path(__file__)), 'scored_definition_instability_warnings': warning_cases,
         'runtime_checkpoint_sha256': digest(data / 'eval-live-namespace-checkpoint-20261007.json'),
         'source_base_commit': xrecord['source_base_commit'],
         'elementtree_backend_observations': xml_backends,
         'output_sha256': {path.name: digest(path) for path in outputs[:-1]}}
def csv_rows(path):
    with path.open(encoding='utf-8-sig', newline='') as stream:
        return list(csv.DictReader(stream))
old_prefix = 'pyperformance-xlang3-super-method-call-vs-cpython3147-full-fast-20261007'
old_subtests = data / (old_prefix + '-subtests.csv')
old_status_path = data / (old_prefix + '-all-97-status.csv')
old_status = {row['benchmark']: row['XLang3 status'] for row in csv_rows(old_status_path)}
new_status = {row['benchmark']: row['XLang3 status'] for row in csv_rows(outputs[0])}
assert len(old_status) == len(new_status) == 97 and old_status.keys() == new_status.keys()
outcome_changes = {
    'failed_to_completed': sorted(name for name in new_status
                                  if old_status[name].startswith('failed:') and new_status[name] == 'completed'),
    'completed_to_failed': sorted(name for name in new_status
                                  if old_status[name] == 'completed' and new_status[name].startswith('failed:')),
    'still_failed': sorted(name for name in new_status
                          if old_status[name].startswith('failed:') and new_status[name].startswith('failed:')),
}
old_scored = {row['subtest'] for row in csv_rows(old_subtests) if row['CPython / XLang3 speedup']}
new_scored = {row['subtest']: row for row in csv_rows(outputs[1]) if row['CPython / XLang3 speedup']}
old_json = data / 'pyperformance-xlang3-super-method-call-full-fast-20261007.json'
historical_load_record = data / 'bigint-division-leftover-process-20261007.json'
before = summary.benchmark_map(json.loads(old_json.read_text(encoding='utf-8')))
after = summary.benchmark_map(json.loads((data / (prefixes['xlang3'] + '.json')).read_text(encoding='utf-8')))
cp = summary.benchmark_map(json.loads((data / (prefixes['cpython3147'] + '.json')).read_text(encoding='utf-8')))
rows = []
for name in sorted(old_scored & new_scored.keys()):
    old, new, reference = (summary.mean(items[name]) for items in (before, after, cp))
    rows.append({'benchmark': new_scored[name]['benchmark'], 'subtest': name,
                 'before_seconds': old, 'after_seconds': new, 'cpython3147_seconds': reference,
                 'before_over_after': old / new, 'cpython_over_after': reference / new})
common = data / 'live-eval-full-common-subtests-20261007.json'
assert not common.exists()
stats = {'common_comparable_subtests': len(rows), 'rows': rows,
         'definition_outcome_changes': outcome_changes,
         'before_over_after_geomean': math.exp(statistics.fmean(math.log(row['before_over_after']) for row in rows)),
         'newly_scored_subtests': sorted(new_scored.keys() - old_scored),
         'old_invalid_genshi_xml_excluded_from_before_after': True,
         'qualifications': ['Multiple engine changes, independent fast-mode samples; not a one-change significance test',
                            'Historical fast samples may have shared CPU load from a leftover task-local Python process discovered and terminated before the new checkpoint measurements',
                            'Old XLang3 source hashes did not cover benchmark Python files at its start; frozen old CPython provenance recorded them'],
         'input_sha256': {path.name: digest(path) for path in
                          (old_status_path, outputs[0], old_subtests, old_json, historical_load_record, outputs[1],
                           data / (prefixes['xlang3'] + '.json'),
                           data / (prefixes['cpython3147'] + '.json'))}}
common.write_text(json.dumps(stats, indent=2) + '\n', encoding='utf-8')
ranked = sorted(rows, key=lambda row: row['before_over_after'], reverse=True)
before_after = ['\n## Changes from the previous full run\n',
                'Previously failed definitions now completed: '
                + (', '.join('`' + name + '`' for name in outcome_changes['failed_to_completed']) or 'none') + '. '
                'Previously completed definitions now failed: '
                + (', '.join('`' + name + '`' for name in outcome_changes['completed_to_failed']) or 'none') + '. '
                + str(len(outcome_changes['still_failed'])) + ' definitions failed in both runs. '
                'These are worker outcomes; completion alone does not certify workload equivalence.\n',
                'On the **' + str(len(rows)) + '** valid scored subtests common to both XLang3 runs, '
                'the geometric mean of old XLang3 time divided by current XLang3 time is **'
                + format(stats['before_over_after_geomean'], '.4f') + '×**. '
                'Above 1× means less time in the current build. '
                'These independent fast-mode samples span multiple engine changes; '
                'they are descriptive and do not prove statistical significance. '
                'The old invalid XML output is excluded from this before/after calculation. '
                'An earlier checkpoint found and terminated a leftover task-local Python process '
                'consuming a CPU core after the historical full runs. Its original script is unrecoverable; '
                'the saved before/after aggregate cannot isolate engine gains from possible load differences. '
                '[Recorded process observation](data/' + historical_load_record.name + '). '
                'The fresh current XLang3-versus-CPython comparison uses the new sequential runs.\n',
                'Largest nominal improvements and slowdowns in that common set:\n',
                '| Subtest | Older XLang3 | Current XLang3 | Old / current |',
                '|---|---:|---:|---:|']
shown = set()
for row in ranked[:5] + sorted(rows, key=lambda row: row['before_over_after'])[:5]:
    if row['subtest'] in shown:
        continue
    shown.add(row['subtest'])
    before_after.append('| `' + row['subtest'] + '` | ' + summary.timing(row['before_seconds'])
                        + ' | ' + summary.timing(row['after_seconds']) + ' | '
                        + format(row['before_over_after'], '.4f') + '× |')
before_after.extend(['', 'Newly scored subtests relative to that earlier valid set: '
                     + ', '.join('`' + name + '`' for name in stats['newly_scored_subtests'])
                     + '. They are included in the current CPython comparison, but not this common-set aggregate.', '',
                     '[Exact common-subtest means and input hashes](data/' + common.name + '). '
                     'The old XLang3 provenance did not hash benchmark Python source files at its start; '
                     'the old CPython provenance recorded them.\n'])
controller_archives = [data / 'pyperformance-live-eval-full-suite-20261007-controller.py',
                       data / 'pyperformance-live-eval-full-report-20261007-controller.py']
controller_sources = [root / 'scratch/performance/run-live-eval-full-suite-20261007.py', Path(__file__)]
for source, archive in zip(controller_sources, controller_archives):
    assert not archive.exists()
    archive.write_bytes(source.read_bytes())
before_after.extend(['\n## Controller sources\n',
                     '[Full-run controller](data/' + controller_archives[0].name + ') and '
                     '[report controller](data/' + controller_archives[1].name + ') preserve '
                     'the exact experiment-specific orchestration and reporting code. '
                     'They contain fixed checkpoint/output guards; they are historical sources, '
                     'not commands to overwrite these saved results.\n'])
report.write_text(report.read_text(encoding='utf-8') + '\n'.join(before_after), encoding='utf-8', newline='\n')
proof['output_sha256'] = {path.name: digest(path) for path in outputs[:-1] + [common] + controller_archives}
proof['report_controller_sha256'] = digest(Path(__file__))
proof['full_suite_controller_sha256'] = digest(controller_sources[0])
outputs[-1].write_text(json.dumps(proof, indent=2) + '\n', encoding='utf-8')
print('Fresh full report and common-subtest evidence saved; visual review and commit remain', flush=True)
