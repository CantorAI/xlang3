"""Publish the completed R4 capture without changing engine or benchmark inputs."""
from __future__ import annotations

import csv
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import shutil
import statistics
import subprocess
import sys

sys.dont_write_bytecode = True
ROOT = next(parent for parent in Path(__file__).resolve().parents
            if (parent / 'benchmarks/diagnostics/summarize_pyperformance_comparison.py').is_file())
DATA = ROOT / 'doc/performance/data'
RAW = 'pyperformance-xlang3-python-new-r4-full-fast-r3-20261009'
OUT = 'pyperformance-xlang3-python-new-r4-vs-cpython3147-full-fast-20261009'
CP_STEM = 'pyperformance-cpython3147-live-eval-full-fast-20261007'
OLD = 'pyperformance-xlang3-dict-scalar-append-vs-cpython3147-full-fast-20261008'
SUMMARY = ROOT / 'benchmarks/diagnostics/summarize_pyperformance_comparison.py'
SUMMARY_SHA = 'c067fcea41ea66be0249209bb86e1ca06c8b9e93b6696076a69d0f0e9aa76255'
CAPTURE_SHA = '797281785559e1ee7d03cb64156edff25714a164e988bbe3362f02d5efc6c8a7'
HEAD = '5997b264f8b71c38b57c98763788702f38146217'

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))

def write_json(path, value):
    Path(path).write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8', newline='\n')

def rows(path):
    with Path(path).open(encoding='utf-8-sig', newline='') as stream:
        return list(csv.DictReader(stream))

def cell(value):
    return str(value).replace('|', '\\|').replace('\r', '').replace('\n', '<br>')

def link(path):
    return Path(path).relative_to(ROOT / 'doc/performance').as_posix()

def main():
    assert sys.version_info[:3] == (3, 14, 7)
    assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
    capture_path = DATA / (RAW + '-provenance.json')
    assert sha(capture_path) == CAPTURE_SHA
    capture = read(capture_path)
    assert capture['terminal'] and capture['status'] == 'finished_with_benchmark_failures'
    assert capture['attempted_definitions'] == capture['header_count'] == 97
    for key in ('hashes_unchanged', 'timing_measurement_valid', 'candidate_release_tree_unchanged',
                'baseline_tree_unchanged', 'accepted_release_tree_unchanged', 'accepted_source_tree_unchanged',
                'head_unchanged', 'benchmark_inputs_unchanged', 'dependency_inputs_unchanged'):
        assert capture[key] is True, key
    raw_row = capture['raw'][0]
    assert raw_row['attempt_capture_valid'] and not raw_row['suite_passed']
    watch = raw_row['external_process_watch']
    assert watch['measurement_valid'] and not watch['overlaps'] and not watch['scanner_errors']
    assert sha(DATA / watch['log']) == watch['sha256']
    assert sha(DATA / (RAW + '.json')) == capture['output_sha256']
    assert sha(DATA / (RAW + '.log')) == capture['log_sha256']
    assert len(capture['failed_definitions']) == 22
    assert list(capture['failed_definitions'].values()).count('Benchmark timed out') == 11
    assert list(capture['failed_definitions'].values()).count('Benchmark died') == 11
    for suffix, expected in capture['cpython_reference']['input_sha256'].items():
        assert sha(DATA / (CP_STEM + suffix)) == expected
    for relative, expected in capture['partial_evidence_sha256'].items():
        assert sha(DATA / relative) == expected
    assert sha(SUMMARY) == SUMMARY_SHA
    assert subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip() == HEAD

    spec = importlib.util.spec_from_file_location('publication_summary', SUMMARY)
    summary = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(summary)
    xmap = summary.benchmark_map(read(DATA / (RAW + '.json')))
    cmap = summary.benchmark_map(read(DATA / (CP_STEM + '.json')))
    assert len(xmap) == 84 and len(cmap) == 124
    reason = 'Traversal work remains uncertified; retain raw timing but withhold a speed score pending independent current workload coverage.'
    exclusion = DATA / (OUT + '-correctness-exclusions.json')
    historical = DATA / 'gc-traversal-coverage-exclusion-20261007.json'
    assert sha(historical) == 'ec8dccd973668436e39c9a3c9585ba4d24bb9c37e75fc97182bd42a399c4eb86'
    write_json(exclusion, {'scope': 'Continued conservative score withholding; not a claim that current GC sources match the historical audit.',
        'unscored_subtests': {'gc_traversal': reason}, 'current_capture_sha256': CAPTURE_SHA,
        'current_collection_benchmark': {'status': 'failed', 'detail': capture['failure_details']['gc_collect']},
        'historical_evidence': historical.name, 'historical_evidence_sha256': sha(historical)})

    command = [sys.executable, '-I', str(SUMMARY), '--xlang-json', str(DATA / (RAW + '.json')),
        '--cpython-json', str(DATA / (CP_STEM + '.json')), '--xlang-log', str(DATA / (RAW + '.log')),
        '--cpython-log', str(DATA / (CP_STEM + '.log')), '--canonical-status', str(DATA / (OLD + '-all-97-status.csv')),
        '--output-dir', str(DATA), '--prefix', OUT,
        '--release-exe-sha256', capture['sha256_end']['exe'], '--release-dll-sha256', capture['sha256_end']['dll'],
        '--case-timeout', '300', '--case-timeout-override', 'networkx*=600s',
        '--invalid-subtest', 'gc_traversal=' + reason, '--correctness-evidence', str(exclusion)]
    generated = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, check=True)
    status_path = DATA / (OUT + '-all-97-status.csv')
    subtest_path = DATA / (OUT + '-subtests.csv')
    statuses = rows(status_path)
    subtests = rows(subtest_path)
    assert len(statuses) == 97 and len({r['benchmark'] for r in statuses}) == 97
    assert sum(r['XLang3 status'] == 'completed' for r in statuses) == 75
    assert sum(r['XLang3 status'].startswith('failed:') for r in statuses) == 22
    assert all(r['CPython 3.14 status'] == 'completed' for r in statuses)
    assert len(subtests) == 84
    scores = []
    for row in subtests:
        name = row['subtest']
        cp = summary.mean(cmap[name])
        xx = summary.mean(xmap[name])
        assert math.isfinite(cp) and math.isfinite(xx) and cp > 0 and xx > 0
        row['CPython 3.14 seconds'] = format(cp, '.17g')
        row['XLang3 seconds'] = format(xx, '.17g')
        row['XLang3 / CPython elapsed time'] = ''
        if name == 'gc_traversal':
            row['result'] = 'unscored: ' + reason
            assert not row['CPython / XLang3 speedup']
        else:
            ratio = cp / xx
            row['CPython / XLang3 speedup'] = format(ratio, '.17g')
            row['XLang3 / CPython elapsed time'] = format(xx / cp, '.17g')
            scores.append((name, cp, xx, ratio))
    assert len(scores) == 83
    with subtest_path.open('w', encoding='utf-8-sig', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(subtests[0]))
        writer.writeheader()
        writer.writerows(subtests)
    geomean = math.exp(statistics.fmean(math.log(r[3]) for r in scores))
    wins = [r for r in scores if r[3] > 1]
    old_status = rows(DATA / (OLD + '-all-97-status.csv'))
    resolved = [r['benchmark'] for r, old in zip(statuses, old_status)
                if r['benchmark'] == old['benchmark'] and r['XLang3 status'] == 'completed'
                and old['XLang3 status'].startswith('failed:')]
    old_map = summary.benchmark_map(read(DATA / (OLD + '-input-xlang.json')))
    old_failed = {r['benchmark'] for r in old_status if r['XLang3 status'].startswith('failed:')}
    current_case = {r['subtest']: r['benchmark'] for r in subtests}
    common = [(name, summary.mean(old_map[name]), xx) for name, cp, xx, ratio in scores
              if name in old_map and current_case[name] not in old_failed]
    common_change = math.exp(statistics.fmean(math.log(old / new) for _, old, new in common))
    partial_rows = []
    for relative in capture['partial_evidence_sha256']:
        path = DATA / relative
        if path.suffix != '.json' or path.name.endswith('.provenance.json'):
            continue
        document = read(path)
        if 'benchmarks' not in document:
            continue
        for name, bench in summary.benchmark_map(document).items():
            partial_rows.append({'file': relative, 'subtest': name,
                'sample_count': len(summary.values(bench)), 'seconds': summary.mean(bench), 'scored': False})
    partial_path = DATA / (OUT + '-failed-definition-partial-values.json')
    write_json(partial_path, {'scope': 'Failed-definition samples are raw evidence only; no speed ratios or aggregate contribution.',
                              'subtests': partial_rows})

    report_path = ROOT / 'doc/performance' / (OUT + '.md')
    chart_path = report_path.with_suffix('.svg')
    svg = chart_path.read_text(encoding='utf-8')
    svg = svg.replace('XLang3 vs CPython 3.14.7', 'XLang3 vs saved CPython 3.14.7')
    svg = svg.replace('Geomean CP time ÷ XLang3 time:', 'Scored-subset geomean CP time ÷ XLang3 time:')
    chart_path.write_text(svg, encoding='utf-8', newline='\n')
    assert svg.count('class="label"') == 83
    report = [
        '# XLang3 versus CPython 3.14.7 — full 97-case checkpoint', '',
        '**All 97 definitions were attempted once: 75 completed and 22 failed (11 timeouts, 11 worker deaths).** The capture is valid; the benchmark suite did not pass. Two previously failed SQLAlchemy definitions now complete.', '',
        f'Of 84 recorded subtests, 83 have speed scores; GC traversal remains unscored. XLang3 has {len(wins)} nominal timing win(s). The successful scored subset has a geometric mean speed of **{geomean:.5f}× CPython**, equivalent to **{1 / geomean:.3f}× elapsed time**. This is not a whole-97 score or evidence that XLang3 is broadly faster.', '',
        '**Read the ratios:** CPython = 1×. Speed is CP time ÷ XLang3 time; higher is faster. Elapsed time is XLang3 time ÷ CP time; lower is faster. For example, 0.05× speed means 20× elapsed time.', '',
        f'![Horizontal speed chart; 1× is CPython, bars right of 1× favor XLang3]({OUT}.svg)', '',
        '## Comparison scope', '',
        'The XLang3 capture is from October 9, 2026. CPython is the saved October 7 run using `C:/Python/Python314/python.exe`, version **3.14.7**, with 97 completed definitions and 124 subtests. These runs are unpaired. Both use pyperformance 1.14.0 fast mode; stability warnings and outliers are retained. Nominal ratios do not establish statistical significance.', '',
        'Historical benchmark Python sources, the compatibility hook, runner and dependency metadata match the recorded reference. Current benchmark/dependency source, data and native bytes are pinned before and after the run. Historical metadata does not retrospectively prove equality of every historical transitive/data byte.', '',
        f'The measured source base is `{HEAD}`. The fixed run path is `D:/CantorAI/xlang3/build-repro/main-verify-20261006/Release/xlang3.exe`. Source identity covers 132 recorded inputs, Release178 and the fixed baseline177, with additional inventories in the receipt; this is not a clean-checkout build claim.', '',
        f"Executable SHA-256: `{capture['sha256_end']['exe']}`. Runtime DLL SHA-256: `{capture['sha256_end']['dll']}`.", '',
        'The entire original selection was run once. Caps were 300 seconds per definition and 600 seconds for `networkx*`, including calibration and setup. The one-second process watcher observed no compiler/CTest overlap or scanner failure; processes entirely between observations may be missed. All terminal source, binary, baseline and dependency checks passed.', '',
        '## Change from the previous XLang3 capture', '',
        f"Coverage improved from 73 to 75 completed definitions. Resolved cases: {', '.join('`' + name + '`' for name in resolved)}. On the {len(common)} common scored subtests, the geometric mean previous-XLang3/current-XLang3 ratio is **{common_change:.5f}×**. This directional comparison spans multiple retained changes and is not a causal constructor result; do not compare aggregates formed from different subsets.", '',
        'The retained dictionary and UTF-8 improvements are runtime changes. The latest class-construction improvement reuses active VM frames for eligible Python `__new__`/`__init__` calls; its balanced component diagnostic showed 1.31×–1.39× over the previous XLang3 control. Lexer, property and closure changes primarily restored correctness. None of these component gains should be presented as CPython speedups.', '',
        '[Constructor checkpoint and fixed regression gate](python-new-vm-continuation-r4-checkpoint-20261009.md).', '',
        '## Every recorded subtest', '',
        f'[Machine-readable means and both ratios]({link(subtest_path)}). Failed-definition partial samples are listed separately and never scored.', '',
        '| Subtest | CPython 3.14.7 | XLang3 | X speed (CP = 1×) | X elapsed (CP = 1×) |',
        '|---|---:|---:|---:|---:|']
    for row in subtests:
        name = row['subtest']
        cp = summary.mean(cmap[name]); xx = summary.mean(xmap[name])
        scored = name != 'gc_traversal'
        report.append(f"| `{cell(name)}` | {summary.timing(cp)} | {summary.timing(xx)} | {cp / xx:.4g}× | {xx / cp:.4g}× |" if scored else
                      f"| `{cell(name)}` | {summary.timing(cp)} | {summary.timing(xx)} | unscored | unscored |")
    report += ['', '## All 97 definition outcomes', '',
        f'[All-97 status CSV]({link(status_path)}). CPython completed every listed definition. A failed definition has no speed score, even if earlier subtests produced values.', '',
        '| Definition | XLang3 outcome | Failure detail |', '|---|---|---|']
    for row in statuses:
        report.append(f"| `{cell(row['benchmark'])}` | {cell(row['XLang3 status'])} | {cell(row['failure detail'])} |")
    report += ['', '## What the remaining gaps mean', '',
        'The full results still show substantial runtime gaps. Indexed locals and X::Value do not remove Python frame entry, dynamic attribute/descriptor dispatch, native boundary work, allocation, or library setup. The full matrix establishes the gaps; it does not establish their individual CPU shares.', '',
        'Coverage loses its trace setting after a nested native-to-Python callback; an independent CPython-first fixture already reproduces that runtime-state mismatch. Its exception-formatting failure is separate. Dask reports a missing `psutil._psutil_windows.virtual_mem` export. Remaining worker tracebacks and timeouts are retained below; a timeout alone does not separate setup from benchmark-body cost.', '',
        'NetworkX loads its graph at module import, outside the algorithm timers. A later unchanged-load diagnostic can separate setup and body costs. The prepared object.__new__ lookup differential keeps the same Python frame entry and instance allocation; it has not been executed and does not justify a cache or projected gain.', '',
        'CPython pure-Python library bodies remain Python. Generic compiler/IR/VM/runtime improvements and own implementations of CPython-native modules are the allowed routes. CPython native DLL reuse is not an optimization route.', '',
        'GC traversal is withheld because equivalent work remains uncertified; its count assertion alone does not prove traversal coverage. This is conservative withholding, not a new claim that current GC code matches the historical audit.', '',
        f'[Current exclusion evidence]({link(exclusion)}). [Failed-definition partial sample list]({link(partial_path)}).', '',
        '## Exact raw evidence', '',
        f'- [XLang3 pyperf JSON]({link(DATA / (RAW + ".json"))}).',
        f'- [Merged raw log]({link(DATA / (RAW + ".log"))}).',
        f'- [Terminal provenance and input inventories]({link(capture_path)}).',
        f'- [One-second process observations]({link(DATA / watch["log"])}).',
        f'- [Saved CPython 3.14.7 JSON]({link(DATA / (CP_STEM + ".json"))}).',
        f'- [Saved CPython status log]({link(DATA / (CP_STEM + ".log"))}).',
        f'- [Saved CPython provenance]({link(DATA / (CP_STEM + "-provenance.json"))}).', '']
    report_path.write_text('\n'.join(report), encoding='utf-8', newline='\n')

    bundle = DATA / (OUT + '-publication')
    bundle.mkdir(exist_ok=False)
    for path in (Path(__file__).resolve(), SUMMARY,
                 ROOT / 'scratch/performance/run-python-new-vm-continuation-r4-full-pyperformance-checkpoint-r3-root-20261009.py',
                 ROOT / 'scratch/performance/prepare-python-new-full97-checkpoint-rebase-root-20261009.py',
                 ROOT / 'scratch/performance/python-new-full97-checkpoint-r3-root-provenance-20261009.json'):
        shutil.copyfile(path, bundle / path.name)
    checks = {'terminal_capture_sha256': CAPTURE_SHA, 'all_97_rows': 97, 'completed': 75, 'failed': 22,
        'recorded_subtests': 84, 'scored_subtests': 83, 'nominal_wins': len(wins), 'scored_subset_speed_geomean': geomean,
        'common_previous_current_subtests': len(common), 'common_previous_current_speed_geomean': common_change,
        'resolved_definitions': resolved, 'partial_subtests_unscored': len(partial_rows),
        'engine_or_benchmark_changes': False, 'reference': 'Saved October 7 CPython3.14.7; unpaired',
        'renderer_sha256': SUMMARY_SHA, 'publisher_sha256': sha(__file__), 'renderer_command': command,
        'renderer_stdout': generated.stdout, 'renderer_stderr': generated.stderr,
        'svg_scored_rows_verified': 83}
    write_json(bundle / 'verification.json', checks)
    owned = [report_path, chart_path, status_path, subtest_path, exclusion, partial_path,
             capture_path, DATA / (RAW + '.json'), DATA / (RAW + '.log'), DATA / watch['log']]
    owned.extend(DATA / p for p in capture['partial_evidence_sha256'])
    owned.extend(p for p in bundle.iterdir() if p.is_file())
    publication = DATA / (OUT + '-publication.json')
    write_json(publication, {'status': 'full_capture_publication_verified', 'measurement_head': HEAD,
        'scope': 'Documentation/data only; no benchmark or engine change; capture validity is not suite success.',
        'checks': checks, 'files_sha256': {p.relative_to(ROOT).as_posix(): sha(p) for p in sorted(set(owned))}})
    assert len(rows(status_path)) == 97 and len(rows(subtest_path)) == 84
    assert sha(capture_path) == CAPTURE_SHA and sha(SUMMARY) == SUMMARY_SHA
    print(json.dumps({'status': 'publication_verified', 'report': str(report_path), 'manifest': str(publication),
                      'manifest_sha256': sha(publication), 'checks': checks}, indent=2))

if __name__ == '__main__':
    main()
