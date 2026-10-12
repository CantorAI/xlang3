"""Authenticate and render a terminal per-definition ledger; no benchmark execution.

Outputs are prepared in a fresh scratch directory for root publication. Invalid
attempts and failed/unstarted definitions never contribute partial speed scores.
The CPython comparison is the authenticated historical October7 fast capture.
"""
from __future__ import annotations

import argparse
import csv
from datetime import datetime, timezone
import hashlib
import html
import importlib.util
import json
import math
from pathlib import Path
import re
import statistics
import sys

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
SCRATCH = ROOT / 'scratch/performance'
CP = Path('C:/Python/Python314/python.exe')
LEDGER_CONTROLLER = SCRATCH / 'run-gc-r7b-all97-per-definition-ledger-proposed-20261009.py'
LEDGER_CONTROLLER_SHA = '48fafce1a804f1970c63e20ce47c66f593686d2df98f2c678ea8a56ec987e4b6'
SUMMARY = ROOT / 'benchmarks/diagnostics/summarize_pyperformance_comparison.py'
SUMMARY_SHA = 'c067fcea41ea66be0249209bb86e1ca06c8b9e93b6696076a69d0f0e9aa76255'
CANONICAL = DATA / 'pyperformance-xlang3-live-eval-vs-cpython3147-full-fast-20261007-all-97-status.csv'
CANONICAL_SHA = '022c1774d346fa381e0b36b7fa4a8d336e7a53466bff85b6073959799d50ab77'
CP_STEM = 'pyperformance-cpython3147-live-eval-full-fast-20261007'
CP_HASHES = {'.json': 'ac474df5576ac85f6f5350363affecc439d4f658b4dfa3dbe291b46964276eb0',
    '.log': '6a1824eed3abad4cebae80356f0f600f1a8728755730d2f64c0bd7d4b3ca9e6c',
    '-provenance.json': '3d6c8cb2c72c786583c382b835e8e411f56ca285963bc2835f757a13f399f85c'}
HEAD = '21e2eadaba7d317932374a63d3c2d70dff4e25c1'
PROTOCOL = 'gc-r7b-per-definition-all97-v1'
RECEIPTS = {'application': '27396bda5dae3e95a7fad24b6c27cd617e092353e323a435987ee3262ef4ee23',
    'build': '41c84cc478c38030f109eaee233ddefe537d3e63ba5638ab36580a8f65bc8852',
    'correctness': '897ce79dd68c514e3c1cc591aa481ce3a71ce1ecf82b48b1393291e7ab37d216',
    'performance': '2b0308d6131394ef7f87bed13c722015b22af1d5b30ab43921bafe2560e39874',
    'accepted_manifest': 'aa2d9f715ad11e78ffa58024c10631c37e7d83d816c4a8cb0523f6469287547f'}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def document(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def map_digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def contained(directory, name):
    relative = Path(name)
    require(not relative.is_absolute() and '..' not in relative.parts, 'Unsafe evidence path')
    path = (directory / relative).resolve(strict=True)
    require(path.is_relative_to(directory.resolve()), 'Evidence escaped its directory')
    return path


def write_json(path, value):
    with path.open('x', encoding='utf-8', newline='\n') as stream:
        stream.write(json.dumps(value, indent=2) + '\n')


def write_csv(path, fields, rows):
    with path.open('x', encoding='utf-8-sig', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def chart(path, comparisons, counts):
    # Same direction as the existing summarizer: arithmetic CP mean / X mean.
    # Only authenticated completed subtests enter the logarithmic native SVG.
    ordered = sorted(comparisons, key=lambda r: (r['speedup'], r['subtest']), reverse=True)
    width, left, right, top, step = 1360, 430, 135, 137, 24
    height = top + max(1, len(ordered)) * step + 80
    ratios = [r['speedup'] for r in ordered]
    low, high = min([.02, *ratios]), max([2., *ratios])
    ticks = sorted(f * 10. ** e for e in range(math.floor(math.log10(low)) - 1,
        math.ceil(math.log10(high)) + 2) for f in (1, 2, 5))
    minimum = max(t for t in ticks if t <= low)
    maximum = min(t for t in ticks if t >= high)
    ticks = [t for t in ticks if minimum <= t <= maximum]
    def x(value):
        return left + (math.log(value) - math.log(minimum)) / (math.log(maximum) - math.log(minimum)) * (width-left-right)
    pieces = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<style>text{font-family:Arial,sans-serif;fill:#172033}.title{font-size:22px;font-weight:bold}.sub{font-size:13px;fill:#4b5563}.label{font-size:12px}.tick{font-size:11px}.grid{stroke:#e5e7eb}.equal{stroke:#172033;stroke-width:2}.fast{fill:#169a70}.slow{fill:#e58b31}</style>',
        '<rect width="100%" height="100%" fill="white"/>',
        '<text x="26" y="33" class="title">Historical CPython time ÷ current XLang3 time — pyperformance fast</text>',
        f'<text x="26" y="57" class="sub">{counts["completed"]}/97 definitions completed; {counts["failed"]} failed; {counts["unfinished"]} unfinished. {len(ordered)} valid completed subtests.</text>',
        '<text x="26" y="79" class="sub">CPython 3.14.7: October7 historical reference. XLang3: independent per-definition windows. Unpaired; different capture protocol.</text>',
        '<text x="26" y="101" class="sub">Right of 1× means shorter XLang3 elapsed time. Failed, invalid and not-run outputs are excluded.</text>']
    bottom = top + max(1, len(ordered)) * step
    for tick in ticks:
        pos = x(tick)
        pieces.append(f'<line x1="{pos:.2f}" y1="{top-15}" x2="{pos:.2f}" y2="{bottom}" class="{"equal" if tick == 1 else "grid"}"/>')
        pieces.append(f'<text x="{pos:.2f}" y="{top-22}" text-anchor="middle" class="tick">{tick:g}×</text>')
    for index, row in enumerate(ordered):
        y, end = top + index*step, x(row['speedup'])
        pieces.extend([f'<text x="{left-10}" y="{y+13}" text-anchor="end" class="label">{html.escape(row["subtest"])}</text>',
            f'<rect x="{left:.2f}" y="{y+2}" width="{max(1., end-left):.2f}" height="15" rx="2" class="{"fast" if row["speedup"] > 1 else "slow"}"/>',
            f'<text x="{width-right+12}" y="{y+13}" class="label">{row["speedup"]:.3f}×</text>'])
    if not ordered:
        pieces.append(f'<text x="{left}" y="{top+14}" class="sub">No authenticated completed subtests available.</text>')
    pieces.extend([f'<text x="{left}" y="{height-42}" class="sub">Logarithmic scale; 1× is equal elapsed time. Bars begin at the plotted minimum, not zero.</text>',
        f'<text x="{left}" y="{height-21}" class="sub">Directional comparison only; no causal engine-gain or universal CPython-win claim.</text>', '</svg>'])
    with path.open('x', encoding='utf-8', newline='\n') as stream:
        stream.write('\n'.join(pieces) + '\n')


def main():
    require(not sys.flags.optimize and Path(sys.executable).resolve() == CP.resolve()
        and sys.version_info[:3] == (3, 14, 7), 'Use fixed unoptimized CPython3.14.7')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ledger', type=Path, required=True)
    parser.add_argument('--ledger-sha256', required=True)
    parser.add_argument('--output-dir', type=Path, required=True, help='Fresh scratch/performance preview directory')
    parser.add_argument('--prefix', default='pyperformance-xlang3-gc-r7b-per-definition-full-fast-20261009')
    args = parser.parse_args()
    require(re.fullmatch(r'[a-z0-9-]+', args.prefix) and re.fullmatch(r'[0-9a-f]{64}', args.ledger_sha256), 'Invalid prefix/hash')
    ledger_path = args.ledger.resolve(strict=True)
    require(ledger_path.is_relative_to(DATA.resolve()), 'Ledger is outside evidence directory')
    output = args.output_dir.resolve()
    require(output.is_relative_to(SCRATCH.resolve()) and output != SCRATCH.resolve()
        and not output.exists(), 'Fresh contained scratch output directory required')
    inputs = {}
    def track(path, expected=None):
        path = Path(path).resolve(strict=True)
        require(path.is_relative_to(ROOT.resolve()) and path.is_file(), 'Unsupported external evidence input')
        value = digest(path)
        require(expected is None or value == expected, 'Evidence hash mismatch: ' + str(path))
        require(str(path) not in inputs or inputs[str(path)] == value, 'Conflicting evidence pin')
        inputs[str(path)] = value
        return path
    track(ledger_path, args.ledger_sha256)
    track(__file__)
    track(LEDGER_CONTROLLER, LEDGER_CONTROLLER_SHA)
    track(SUMMARY, SUMMARY_SHA)
    track(CANONICAL, CANONICAL_SHA)
    spec = importlib.util.spec_from_file_location('ledger_report_summary', SUMMARY)
    summary = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(summary)
    ledger = document(ledger_path)
    binding = ledger['binding']
    require(ledger['terminal'] and ledger['protocol'] == binding['protocol'] == PROTOCOL,
        'Only an explicitly terminal ledger may be reported')
    require(binding['head'] == HEAD and binding['controller_sha256'] == LEDGER_CONTROLLER_SHA
        and binding['canonical_sha256'] == CANONICAL_SHA and binding['receipt_hashes'] == RECEIPTS,
        'Different candidate/controller lineage')
    require(ledger['binding_digest'] == map_digest(binding) and ledger['max_child_attempts_per_definition'] == 3,
        'Binding/retry policy differs')
    require(len(binding['source_sha256']) == 143 and len(binding['release_sha256']) == 178
        and len(binding['baseline_sha256']) == 177, 'Source/Release/baseline population differs')
    accepted_receipts = {label: document(track(binding['receipt_paths'][label], expected))
        for label, expected in RECEIPTS.items()}
    app, build, correct, perf, accepted = (accepted_receipts[k] for k in
        ('application', 'build', 'correctness', 'performance', 'accepted_manifest'))
    require(binding['source_sha256'] == app['source_sha256'] == build['source_sha256'] == correct['source_sha256']
        == accepted['source_sha256'] and binding['release_sha256'] == build['release_sha256']
        == correct['release_sha256'] == accepted['release_sha256'] and binding['baseline_sha256']
        == app['fixed_baseline_sha256'] == accepted['fixed_baseline_sha256'], 'Recorded candidate maps differ')
    require(build['terminal'] and build['passed'] and build['exit_code'] == 0 and correct['correctness_passed']
        and perf['terminal'] and perf['hashes_unchanged'] and perf['status'] == 'gate_passed_original_gc_completed'
        and accepted['terminal'] and accepted['full_validated'] and accepted['fixed_gate_passed'],
        'Accepted candidate prerequisite evidence differs')
    for suffix, expected in CP_HASHES.items():
        track(DATA / (CP_STEM + suffix), expected)
    cp_data = document(DATA / (CP_STEM + '.json'))
    cp_prov = document(DATA / (CP_STEM + '-provenance.json'))
    require(cp_prov['status'] == 'finished' and cp_prov['exit_code'] == 0 and cp_prov['runtime_version'] == '3.14.7'
        and cp_prov['expected_definitions'] == cp_prov['attempted_definitions'] == 97
        and cp_prov['recorded_subtests'] == 124 and not cp_prov['failed_definitions']
        and cp_prov['mode'] == 'fast' and cp_prov['case_timeout_seconds'] == 300
        and cp_prov['case_timeout_overrides'] == {'networkx*': 600}, 'Historical reference policy differs')
    require(ledger['historical_cpython']['sha256'] == CP_HASHES, 'Ledger historical reference differs')
    with CANONICAL.open(encoding='utf-8-sig', newline='') as stream:
        canonical = list(csv.DictReader(stream))
    definitions = [r['benchmark'] for r in canonical]
    require(len(definitions) == len(set(definitions)) == 97 and binding['definitions'] == definitions
        and [r['definition'] for r in ledger['cases']] == definitions and ledger['expected_definitions'] == 97,
        'Canonical definition list/order differs')
    expected_names = {r['benchmark']: summary.parse_subtests(r['CPython subtests']) for r in canonical}
    require(binding['expected_subtests'] == expected_names and all(r['CPython 3.14 status'] == 'completed' for r in canonical),
        'Expected definition/subtest mapping differs')
    cp_map = summary.benchmark_map(cp_data)
    ordered_names = [name for names in expected_names.values() for name in names]
    require(len(ordered_names) == len(set(ordered_names)) == 124 and set(ordered_names) == set(cp_map), 'Historical subtests differ')
    require(set(summary.case_sections((DATA / (CP_STEM + '.log')).read_text(encoding='utf-8'))) == set(definitions),
        'Historical definition log differs')
    expected_samples, cp_means = {}, {}
    for name, benchmark in cp_map.items():
        metadata = {**cp_data.get('metadata', {}), **benchmark.get('metadata', {})}
        values = summary.values(benchmark)
        require(values and all(math.isfinite(v) and v > 0 for v in values) and metadata.get('unit') == 'second',
            'Unsupported historical unit/sample population')
        expected_samples[name] = {'unit': metadata['unit'], 'scored_value_count': len(values)}
        cp_means[name] = statistics.fmean(values)
    require(binding['expected_subtest_samples'] == expected_samples, 'Scored count/unit binding differs')
    pin_digest = map_digest(ledger['pins'])
    def identity_ok(value):
        return value.get('passed') and value.get('head') == HEAD and value.get('checked_file_count') == len(ledger['pins']) \
            and value.get('expected_sha256_map_digest') == pin_digest and value.get('observed_sha256_map_digest') == pin_digest \
            and not value.get('mismatches') and not value.get('tree_changes')
    terminal_identity_passed = bool(identity_ok(ledger['terminal_identity']))
    status_rows, subtest_rows, attempt_rows, comparisons = [], [], [], []
    completed, failed, invalid_attempts, unfinished_attempts = 0, [], 0, 0
    recorded_names, retained_count = set(), 0
    for case in ledger['cases']:
        definition = case['definition']
        require(case['status'] in ('pending', 'completed', 'failed', 'invalid_pending', 'invalid_exhausted'), 'Unknown case status')
        ids = [a['attempt_id'] for a in case['attempts']]
        require(len(ids) == len(set(ids)) and (case['retained_attempt'] is None or case['retained_attempt'] in ids),
            'Duplicate/missing retained attempt')
        retained, case_invalid, case_unfinished, launched = None, 0, 0, 0
        for entry in case['attempts']:
            aid = entry['attempt_id']
            require(re.fullmatch(r'[a-z0-9_-]+', aid) and entry['receipt'] == aid + '.attempt.json', 'Unsafe attempt identity')
            if entry['state'] != 'finished':
                require(case['retained_attempt'] != aid, 'Unfinished attempt cannot be retained')
                unfinished_attempts += 1
                case_unfinished += 1
                attempt_rows.append({'benchmark': definition, 'attempt_id': aid, 'state': entry['state'],
                    'valid': False, 'definition_status': 'unfinished_unverified', 'child_launched': entry.get('child_launched', ''),
                    'retained': False, 'receipt': entry['receipt'], 'receipt_sha256': '', 'reason': 'No immutable finished receipt; excluded',
                    'raw_file_count': '', 'timing_scoring_permitted': False})
                continue
            receipt_path = contained(DATA, entry['receipt'])
            track(receipt_path, entry['sha256'])
            row = document(receipt_path)
            require(row['state'] == 'finished' and row['attempt_id'] == aid and row['definition'] == definition
                and row['candidate_binding_digest'] == ledger['binding_digest'] and row['valid'] == entry['valid'], 'Attempt association differs')
            for name, expected in row['raw_sha256'].items():
                track(contained(DATA, name), expected)
            child_launched = bool(row.get('child_launched', False))
            require(child_launched == entry.get('child_launched', False), 'Attempt launch record differs')
            launched += child_launched
            if not row['valid']:
                require(row['definition_status'] == 'invalid' and case['retained_attempt'] != aid
                    and row['timing_scoring_permitted'] is False, 'Invalid attempt was scored/retained')
                invalid_attempts += 1
                case_invalid += 1
            else:
                require(row['definition_status'] in ('completed', 'failed') and child_launched
                    and row['measurement_valid'] and row['owned_child_cleanup_completed']
                    and all(row[k]['passed'] and not row[k]['busy'] for k in ('pre_idle', 'launch_idle', 'post_idle'))
                    and identity_ok(row['pre_identity']) and identity_ok(row['post_identity']), 'Valid attempt guard evidence differs')
                require(not any(k in row for k in ('error', 'raw_hash_error', 'cleanup_error', 'post_guard_error', 'watch_finish_error')),
                    'Valid attempt contains failure bookkeeping')
                watch = row['external_process_watch']
                require(watch['measurement_valid'] and not watch['overlaps'] and not watch['scanner_errors']
                    and row['raw_sha256'].get(watch['log']) == watch['sha256'], 'Valid watcher evidence differs')
                observations = [json.loads(line) for line in contained(DATA, watch['log']).read_text(encoding='utf-8').splitlines()]
                require(observations and all(not o.get('busy') and 'error' not in o for o in observations), 'Raw watcher has overlap/error')
                log_name, output_name = aid + '.merged.log', aid + '.pyperf.json'
                require(log_name in row['raw_sha256'], 'Valid attempt lacks authenticated original log')
                log = contained(DATA, log_name).read_text(encoding='utf-8', errors='replace')
                headers = [summary.CASE_LINE.match(line).group(1) for line in log.splitlines() if summary.CASE_LINE.match(line)]
                failures, details = summary.failure_details(log)
                require(headers == [definition] and set(summary.case_sections(log)) == {definition}, 'Original definition log differs')
                command = [str(CP), str(ROOT / 'benchmarks/diagnostics/run_pyperformance_xlang3_shimmed.py'), '--runtime',
                    str(ROOT / 'build-repro/main-verify-20261006/Release/xlang3.exe'), '--benchmarks', definition,
                    '--mode', 'fast', '--case-timeout', '300', '--case-timeout-override', 'networkx*=600',
                    '--dependency-site', str(ROOT / 'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages'),
                    '--output', str(DATA / output_name)]
                require(row['command'] == command and row['case_cap_seconds'] == (600 if definition.startswith('networkx') else 300),
                    'Original runner command/cap differs')
                if row['definition_status'] == 'failed':
                    require(row['exit_code'] != 0 and set(failures) == {definition}
                        and row['partial_timings_never_scored'] and row['timing_scoring_permitted'] is False,
                        'Genuine final failure evidence differs')
                else:
                    require(row['exit_code'] == 0 and not failures and row['timing_scoring_permitted'] is True
                        and output_name in row['raw_sha256'], 'Completed output is invalid or unauthenticated')
                    raw = document(contained(DATA, output_name))
                    benchmarks = summary.benchmark_map(raw)
                    require(set(benchmarks) == set(expected_names[definition]), 'Completed subtests differ')
                    for name, benchmark in benchmarks.items():
                        metadata = {**raw.get('metadata', {}), **benchmark.get('metadata', {})}
                        values = summary.values(benchmark)
                        require(metadata.get('unit') == expected_samples[name]['unit']
                            and len(values) == expected_samples[name]['scored_value_count']
                            and all(math.isfinite(v) and v > 0 for v in values), 'Completed count/unit/values differ')
                    require(row['verified_sample_population'] == {name: expected_samples[name] for name in benchmarks},
                        'Receipt sample proof differs')
                require(case['retained_attempt'] == aid, 'Valid finished definition was not retained')
                require(retained is None and case['status'] == row['definition_status'], 'Multiple retained outcomes')
                retained = row
            reason = row.get('failure_detail') or row.get('failure') or row.get('error') or row.get('classification_error') \
                or row.get('post_guard_error') or row.get('watch_finish_error') or ''
            attempt_rows.append({'benchmark': definition, 'attempt_id': aid, 'state': row['state'], 'valid': row['valid'],
                'definition_status': row['definition_status'], 'child_launched': child_launched,
                'retained': case['retained_attempt'] == aid, 'receipt': entry['receipt'], 'receipt_sha256': entry['sha256'],
                'reason': reason, 'raw_file_count': len(row['raw_sha256']), 'timing_scoring_permitted': row['timing_scoring_permitted']})
        require(launched <= 3, 'Launch budget exceeded')
        require((case['retained_attempt'] is not None) == (retained is not None), 'Retained association is missing')
        label = case['status'] if case['attempts'] else 'not_run'
        if case_unfinished:
            label = 'unfinished_unverified'
        xmap, failure_detail = {}, ''
        if retained is not None:
            retained_count += 1
            if retained['definition_status'] == 'completed':
                completed += 1
                xmap = summary.benchmark_map(document(contained(DATA, retained['attempt_id'] + '.pyperf.json')))
            else:
                failed.append(definition)
                failure_detail = retained.get('failure_detail') or retained.get('failure', '')
        elif case['status'] in ('completed', 'failed'):
            raise RuntimeError('Final status without valid retained evidence')
        xdisplay = []
        for name in expected_names[definition]:
            xmean = statistics.fmean(summary.values(xmap[name])) if name in xmap else None
            ratio = cp_means[name] / xmean if xmean is not None else None
            if ratio is not None:
                require(math.isfinite(ratio) and ratio > 0 and name not in recorded_names, 'Invalid/duplicate completed score')
                recorded_names.add(name)
                comparisons.append({'benchmark': definition, 'subtest': name, 'speedup': ratio})
                xdisplay.append(name + '=' + summary.timing(xmean))
            subtest_rows.append({'benchmark': definition, 'subtest': name, 'XLang3 status': label,
                'CPython 3.14.7 historical seconds': cp_means[name], 'XLang3 seconds': xmean if xmean is not None else '',
                'CPython / XLang3 speedup': ratio if ratio is not None else '',
                'XLang3 / CPython elapsed factor': xmean / cp_means[name] if xmean is not None else '',
                'unit': expected_samples[name]['unit'],
                'CPython scored values': expected_samples[name]['scored_value_count'],
                'XLang3 scored values': expected_samples[name]['scored_value_count'] if ratio is not None else '',
                'comparison': 'historical unpaired fast evidence' if ratio is not None else 'not scored'})
        status_rows.append({'benchmark': definition, 'CPython 3.14.7 status': 'historical completed',
            'CPython subtests': '; '.join(n + '=' + summary.timing(cp_means[n]) for n in expected_names[definition]),
            'XLang3 status': label, 'XLang3 subtests': '; '.join(xdisplay), 'failure detail': failure_detail,
            'attempts': len(case['attempts']), 'launched attempts': launched, 'invalid attempts': case_invalid,
            'unfinished attempts': case_unfinished, 'retained attempt': case['retained_attempt'] or '',
            'raw receipt': (case['retained_attempt'] + '.attempt.json') if retained else '',
            'raw result': (case['retained_attempt'] + '.pyperf.json') if xmap else ''})
    require(len(status_rows) == 97 and len(subtest_rows) == 124 and ledger['valid_finished_definitions'] == retained_count
        and ledger['completed_definitions'] == completed and ledger['failed_definitions'] == failed,
        'Terminal counts differ from authenticated outcomes')
    require(ledger['capture_complete'] == (retained_count == 97 and terminal_identity_passed)
        and ledger['suite_passed'] == (ledger['capture_complete'] and not failed), 'Complete/suite status differs')
    counts = {'completed': completed, 'failed': len(failed), 'unfinished': 97-retained_count,
        'valid_finished_definitions': retained_count, 'invalid_attempts': invalid_attempts,
        'unfinished_attempts': unfinished_attempts, 'scored_subtests': len(comparisons)}
    ratios = [r['speedup'] for r in comparisons]
    geomean = math.exp(statistics.fmean(math.log(r) for r in ratios)) if ratios else None
    wins, losses = sum(r > 1 for r in ratios), sum(r < 1 for r in ratios)
    # Authenticate every input again before creating preview outputs. Rendering
    # cannot turn an active or changed ledger into a certified report.
    for path, expected in inputs.items():
        require(digest(path) == expected, 'Evidence changed during report authentication')
    output.mkdir(parents=True, exist_ok=False)
    names = {key: args.prefix + suffix for key, suffix in (
        ('status', '-all-97-status.csv'), ('subtests', '-all-124-subtests.csv'),
        ('attempts', '-all-attempts.csv'), ('chart', '-speedup.svg'), ('report', '.md'), ('provenance', '-report-provenance.json'))}
    write_csv(output / names['status'], list(status_rows[0]), status_rows)
    write_csv(output / names['subtests'], list(subtest_rows[0]), subtest_rows)
    write_csv(output / names['attempts'], ['benchmark', 'attempt_id', 'state', 'valid', 'definition_status', 'child_launched',
        'retained', 'receipt', 'receipt_sha256', 'reason', 'raw_file_count', 'timing_scoring_permitted'], attempt_rows)
    chart(output / names['chart'], comparisons, counts)
    rows_md = '\n'.join('| ' + r['benchmark'] + ' | ' + r['XLang3 status'] + ' | ' + str(r['attempts']) + ' | ' \
        + r['failure detail'].replace('|', '\\|').replace('\n', ' ') + ' |' for r in status_rows)
    subtests_md = '\n'.join('| ' + r['benchmark'] + ' | ' + r['subtest'] + ' | '
        + summary.timing(r['CPython 3.14.7 historical seconds']) + ' | '
        + (summary.timing(r['XLang3 seconds']) if r['XLang3 seconds'] != '' else '') + ' | '
        + (f"{r['CPython / XLang3 speedup']:.6f}×" if r['CPython / XLang3 speedup'] != '' else '') + ' | '
        + (f"{r['XLang3 / CPython elapsed factor']:.6f}×" if r['XLang3 / CPython elapsed factor'] != '' else '') + ' | '
        + r['XLang3 status'] + ' | ' + ('scored' if r['XLang3 seconds'] != '' else 'unscored') + ' |'
        for r in subtest_rows)
    geomean_text = f'{geomean:.6f}×' if geomean is not None else 'unavailable'
    ledger_relative = ledger_path.relative_to(DATA).as_posix()
    report = f'''# Generic-GC R7b per-definition pyperformance capture

The terminal ledger records {completed}/97 completed definitions, {len(failed)} final benchmark failures and {97-retained_count} unfinished definitions. Capture complete: `{ledger['capture_complete']}`. Suite passed: `{ledger['suite_passed']}`. This report retains all97 statuses and all124 expected historical subtests; it scores only {len(comparisons)} authenticated completed subtests.

The comparison uses historical October7 CPython3.14.7 fast results. Across the completed subset, CP time ÷ current XLang3 time has geometric mean {geomean_text}: {wins} ratios above1, {losses} below1 and {len(ratios)-wins-losses} equal. A CP/X speed above1 means shorter XLang3 elapsed time. The table also shows the reciprocal X/CP elapsed factor: above1 means XLang3 took longer. This is unpaired evidence from different dates and capture protocols, not a causal engine-gain estimate or a universal CPython-win claim. Failed, invalid, incomplete and not-run outputs contribute no partial scores.

![Historical CP time divided by current XLang3 time]({names['chart']})

[All97 statuses](data/{names['status']}), [all124 expected subtests](data/{names['subtests']}) and [every recorded attempt](data/{names['attempts']}) retain unsuccessful outcomes. The [terminal ledger](data/{ledger_relative}) SHA256 is `{args.ledger_sha256}`. [Report authentication](data/{names['provenance']}) lists every verified receipt/raw input hash.

## Capture protocol and limits

The unchanged original shimmed runner invokes one definition per fresh manager with `--benchmarks NAME --mode fast --case-timeout 300 --case-timeout-override networkx*=600`. Original datasets, Python bodies, workers and scored populations are unchanged. Per-definition process windows and later explicit resumptions differ from the old single-manager full97 capture; order/cache/host differences remain possible. The saved CP reference has97 completed definitions and124 scored subtests. Historical source, hook and dependency METADATA identities do not prove every historical transitive/data/native byte.

Each final valid attempt authenticates exact raw result/log/activity bytes, original command, expected subtests, merged metadata units, scored-value counts, before/after candidate identity, cleanup and activity guards. Valid benchmark failures are final. {invalid_attempts} invalid attempts and {unfinished_attempts} unfinished unverified attempts remain explicit and excluded. Known orphan admission, if used, applies only to its exact creation identity with continuously unchanged cumulative CPU counters and permitted children; no foreign process is terminated. One-second waits plus OS scan time can miss short foreign processes. Unseen orphan descendants fail closed.

The accepted capture HEAD is `{HEAD}`. Recorded scope is source143, complete candidate Release178, fixed baseline177 and preserved143/178, including six preexisting unowned dirty inputs. This is not a clean-checkout claim. The ledger's terminal identity passed: `{terminal_identity_passed}`. This adapter authenticates recorded evidence; it does not load binaries, revalidate the engine currently on disk, execute benchmarks or refresh the historical CP reference. Prerequisite engine correctness/gate checks do not prove every original benchmark's semantics exhaustively.

## Every original definition

| Definition | XLang3 status | Recorded attempts | Failure detail |
| --- | --- | ---: | --- |
{rows_md}

## Every expected subtest: complete comparison matrix

Both time columns are arithmetic means of the original scored values. CPython3.14.7 is the historical October7 reference. CP/X speed above1 means XLang3 is faster; X/CP elapsed factor above1 means XLang3 is slower. An unsuccessful X outcome leaves its time and both ratio cells blank and is explicitly unscored.

| Definition | Subtest | CPython3.14.7 time | XLang3 time | CP/X speed (>1 X faster) | X/CP elapsed factor (>1 X slower) | XLang3 status | Score |
| --- | --- | ---: | ---: | ---: | ---: | --- | --- |
{subtests_md}
'''
    with (output / names['report']).open('x', encoding='utf-8', newline='\n') as stream:
        stream.write(report)
    provenance = {'status': 'authenticated_terminal_ledger_report_preview', 'created_utc': datetime.now(timezone.utc).isoformat(),
        'ledger': str(ledger_path), 'ledger_sha256': args.ledger_sha256, 'ledger_status': ledger['status'],
        'capture_complete': ledger['capture_complete'], 'suite_passed': ledger['suite_passed'], 'counts': counts,
        'comparison': {'reference': CP_STEM, 'version': '3.14.7', 'historical_unpaired': True, 'geomean_completed_subset': geomean,
            'faster_subtests': wins, 'slower_subtests': losses, 'equal_subtests': len(ratios)-wins-losses,
            'score': 'Arithmetic historical CP mean divided by arithmetic current XLang3 mean; geometric mean over completed subset only'},
        'candidate_binding': binding, 'terminal_identity': ledger['terminal_identity'], 'input_sha256': inputs,
        'scope': 'Evidence rendering only. No binary loading, benchmark/AST/build execution, engine edits or Git mutation.'}
    write_json(output / names['provenance'], provenance)
    for path, expected in inputs.items():
        require(digest(path) == expected, 'Evidence changed while rendering; preview must not be published')
    mappings = []
    for key, name in names.items():
        destination = ('doc/performance/' if key in ('chart', 'report') else 'doc/performance/data/') + name
        mappings.append({'source': (output/name).relative_to(ROOT).as_posix(), 'destination': destination,
            'sha256': digest(output/name), 'bytes': (output/name).stat().st_size})
    manifest = output / (args.prefix + '-report-preview-manifest.json')
    write_json(manifest, {'status': 'verified_preview_only_not_published', 'ledger_sha256': args.ledger_sha256,
        'controller_sha256': digest(__file__), 'files': mappings, 'file_count': len(mappings),
        'input_hashes_unchanged': True, 'publication': 'Root independently reviews and copies/stages exact owned doc paths; this adapter does not publish.'})
    print(json.dumps({'preview_manifest': str(manifest), 'sha256': digest(manifest), 'counts': counts,
        'capture_complete': ledger['capture_complete'], 'suite_passed': ledger['suite_passed']}, indent=2), flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
