"""File-only origin authentication shared by the held supplement and report."""
from __future__ import annotations
import csv
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re
import statistics

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
SCRATCH = ROOT / 'scratch/performance'
PRODUCER = SCRATCH / 'run-gc-r7b-all97-per-definition-ledger-proposed-20261009.py'
PRODUCER_SHA = '48fafce1a804f1970c63e20ce47c66f593686d2df98f2c678ea8a56ec987e4b6'
REPORT = SCRATCH / 'report-gc-r7b-all97-per-definition-ledger-r2-proposed-20261009.py'
REPORT_SHA = '391297e73f5dfd0faa36f2f2200b94b581a97b9bb5965a3e47ae1aa338c5fec8'
PREFIX = 'pyperformance-xlang3-gc-r7b-per-definition-ownership-supplement-20261009'
PROTOCOL = 'gc-r7b-per-definition-all97-ownership-supplement-v2-r3'
PARSER_POLICY = 'Exact canonical single [1/1] header; malformed/multiple/mismatched headers and contradictory/duplicate known markers raise invalid-log RuntimeError; coherent same-name footer plus optional matching official marker, or named timeout/Benchmark-died plus No-benchmark-was-run; nonzero exit required for final failure; all partial timings unscored'
PARSER_SOURCE_SHA = {
    'C:/Python/Python314/Lib/site-packages/pyperformance/run.py': '1090a1db32d4b0e57cd81a5ca087efff164bc50d84d1ff7a9a7a09dde28cf37a',
    'C:/Python/Python314/Lib/site-packages/pyperformance/commands.py': 'ef6fe8eed2d82db8b1fcb445cb8062aab1c2dc183ac26636711de36c7820cdf5'}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def doc(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def mdigest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def contained(directory, name):
    p = Path(name)
    require(not p.is_absolute() and '..' not in p.parts, 'Unsafe origin path')
    p = (directory/p).resolve(strict=True)
    require(p.is_relative_to(directory.resolve()), 'Origin path escaped')
    return p


def load(path, expected, name):
    require(digest(path) == expected, 'Frozen helper/producer hash mismatch')
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def primitives():
    base = load(PRODUCER, PRODUCER_SHA, 'supplement_base')
    report = load(REPORT, REPORT_SHA, 'supplement_report_base')
    summary = load(report.SUMMARY, report.SUMMARY_SHA, 'supplement_summary')
    return base, report, summary


def identity(value, ledger):
    expected = mdigest(ledger['pins'])
    return value.get('passed') and value.get('head') == ledger['binding']['head'] \
        and value.get('checked_file_count') == len(ledger['pins']) \
        and value.get('expected_sha256_map_digest') == value.get('observed_sha256_map_digest') == expected \
        and not value.get('mismatches') and not value.get('tree_changes')


def prospective_failure_details(summary, text, definition):
    """New-origin parser only; exit/status guards stay with the actual caller.

    Raise on invalid evidence, rather than returning the clean-success sentinel.
    The frozen phase catches this as an invalid unscored attempt; authentication
    rejects it too. Partial failed means are legitimate and never become scores.
    Exit/status guards remain separate: not failures is required for success,
    while a final failure requires nonzero exit and exact definition membership.
    """
    def invalid(reason):
        raise RuntimeError('Invalid prospective log: ' + reason)

    lines = text.splitlines()
    header = re.compile(r'^\s*\[\s*1/1\]\s+([A-Za-z0-9_]+)\.\.\.\s*$')
    # Also notice malformed progress headers; they must not disappear beside a
    # valid one and make a multi-header log look like a clean single definition.
    header_start = re.compile(r'^\s*\[\s*[^\]\r\n]*/')
    headers = [line for line in lines if summary.CASE_LINE.match(line) or header_start.match(line)]
    if len(headers) != 1:
        invalid('Expected exactly one progress header')
    match = header.fullmatch(headers[0])
    if match is None or match.group(1) != definition:
        invalid('Header is not the selected canonical [1/1] definition')

    # Count occurrences before using the legacy dict parser, which collapses
    # duplicate footers. Validate all known markers before choosing either route.
    footers = summary.FAILURE_LINE.findall(text)
    timeout = re.compile(r'ERROR: Benchmark (.+?) timed out')
    died = re.compile(r'ERROR: Benchmark (.+?) failed: Benchmark died')
    named = []
    for line in lines:
        timed, dead = timeout.fullmatch(line), died.fullmatch(line)
        if timed: named.append((timed.group(1), 'Benchmark timed out', line))
        if dead: named.append((dead.group(1), 'Benchmark died', line))
    no_suite = lines.count('ERROR: No benchmark was run')
    if len(footers) > 1 or len(named) > 1 or no_suite > 1:
        invalid('Duplicate known failure evidence')
    if any(name != definition for name, _ in footers) or any(name != definition for name, _, _ in named):
        invalid('Known failure marker names another definition')
    if footers and named and footers[0][1] != named[0][1]:
        invalid('Footer and official marker disagree on failure reason')
    if footers:
        # A partial suite can print both the named error and footer, without the
        # no-suite line. Keep its failure evidence; its partial means are unscored.
        legacy, legacy_details = summary.failure_details(text)
        if legacy != {definition:footers[0][1]}:
            invalid('Legacy footer parser disagrees with validated evidence')
        return legacy, legacy_details
    if named:
        if no_suite != 1:
            invalid('Named-only failure lacks exactly one no-suite marker')
        return {definition:named[0][1]}, {definition:named[0][2]}
    if no_suite:
        invalid('No-suite marker lacks a proven named failure')
    return {}, {}


def require_final_failure(row, failures, definition):
    require(row['definition_status'] == 'failed' and row['exit_code'] != 0 and set(failures) == {definition}
        and row['partial_timings_never_scored'] and row['timing_scoring_permitted'] is False,
        'Genuine final failure evidence/status/exit differs')


def reference(report, summary, binding):
    for suffix, expected in report.CP_HASHES.items():
        require(digest(DATA/(report.CP_STEM+suffix)) == expected, 'Historical CP raw changed')
    require(digest(report.CANONICAL) == report.CANONICAL_SHA, 'Canonical97 changed')
    with report.CANONICAL.open(encoding='utf-8-sig', newline='') as stream:
        canonical = list(csv.DictReader(stream))
    definitions = [r['benchmark'] for r in canonical]
    names = {r['benchmark']: summary.parse_subtests(r['CPython subtests']) for r in canonical}
    require(len(definitions) == len(set(definitions)) == 97 and binding['definitions'] == definitions
        and names == binding['expected_subtests'] and all(r['CPython 3.14 status'] == 'completed' for r in canonical), 'Canonical binding differs')
    cpraw = doc(DATA/(report.CP_STEM+'.json'))
    cp = doc(DATA/(report.CP_STEM+'-provenance.json'))
    require(cp['status'] == 'finished' and cp['exit_code'] == 0 and cp['runtime_version'] == '3.14.7'
        and cp['attempted_definitions'] == cp['expected_definitions'] == 97 and cp['recorded_subtests'] == 124
        and not cp['failed_definitions'] and cp['mode'] == 'fast' and cp['case_timeout_seconds'] == 300
        and cp['case_timeout_overrides'] == {'networkx*': 600}, 'Historical CP policy differs')
    cpmap = summary.benchmark_map(cpraw)
    ordered = [n for d in definitions for n in names[d]]
    require(len(ordered) == len(set(ordered)) == 124 and set(ordered) == set(cpmap), 'Historical subtests differ')
    require(set(summary.case_sections((DATA/(report.CP_STEM+'.log')).read_text(encoding='utf-8'))) == set(definitions), 'Historical definition log differs')
    samples, means = {}, {}
    for name, benchmark in cpmap.items():
        values = summary.values(benchmark)
        unit = {**cpraw.get('metadata', {}), **benchmark.get('metadata', {})}.get('unit')
        require(unit == 'second' and values and all(math.isfinite(v) and v > 0 for v in values), 'CP sample population differs')
        samples[name] = {'unit': unit, 'scored_value_count': len(values)}
        means[name] = statistics.fmean(values)
    require(samples == binding['expected_subtest_samples'], 'Expected scored counts/units differ')
    return means


def attempt(entry, case, ledger, base, summary, inputs):
    path = contained(DATA, entry['receipt'])
    require(digest(path) == entry['sha256'], 'Origin receipt changed')
    inputs[str(path)] = entry['sha256']
    row = doc(path)
    require(entry['state'] == row['state'] == 'finished' and row['attempt_id'] == entry['attempt_id']
        and row['definition'] == case['definition'] and row['candidate_binding_digest'] == ledger['binding_digest']
        and row['valid'] == entry['valid'] and row.get('child_launched', False) == entry.get('child_launched', False), 'Origin attempt association differs')
    for name, expected in row['raw_sha256'].items():
        p = contained(DATA, name)
        require(digest(p) == expected, 'Origin raw changed')
        inputs[str(p)] = expected
    if not row['valid']:
        require(row['definition_status'] == 'invalid' and row['timing_scoring_permitted'] is False, 'Invalid origin was scored')
        return row, {}
    require(row['definition_status'] in ('completed', 'failed') and row.get('child_launched') and row['measurement_valid']
        and row['owned_child_cleanup_completed'] and identity(row['pre_identity'], ledger) and identity(row['post_identity'], ledger)
        and all(row[k]['passed'] and not row[k]['busy'] for k in ('pre_idle', 'launch_idle', 'post_idle')), 'Origin guard invalid')
    require(not any(k in row for k in ('error', 'raw_hash_error', 'cleanup_error', 'post_guard_error', 'watch_finish_error')), 'Valid origin has error')
    watch = row['external_process_watch']
    require(watch['measurement_valid'] and not watch['overlaps'] and not watch['scanner_errors']
        and row['raw_sha256'].get(watch['log']) == watch['sha256'], 'Origin watch invalid')
    observations = [json.loads(line) for line in contained(DATA, watch['log']).read_text(encoding='utf-8').splitlines()]
    require(observations and all(not o.get('busy') and 'error' not in o for o in observations), 'Raw watch overlap/error')
    aid = entry['attempt_id']
    log_name, output_name = aid+'.merged.log', aid+'.pyperf.json'
    require(log_name in row['raw_sha256'], 'Origin log unauthenticated')
    text = contained(DATA, log_name).read_text(encoding='utf-8', errors='replace')
    headers = [summary.CASE_LINE.match(s).group(1) for s in text.splitlines() if summary.CASE_LINE.match(s)]
    definition = case['definition']
    failures, details = prospective_failure_details(summary,text,definition) if ledger['protocol'] == PROTOCOL else summary.failure_details(text)
    require(headers == [definition] and set(summary.case_sections(text)) == {definition}, 'Origin selected definition differs')
    command = [str(base.CP), str(base.RUNNER), '--runtime', str(base.CANDIDATE), '--benchmarks', definition, '--mode', 'fast',
        '--case-timeout', '300', '--case-timeout-override', 'networkx*=600', '--dependency-site', str(base.SITE), '--output', str(DATA/output_name)]
    require(row['command'] == command and row['case_cap_seconds'] == (600 if definition.startswith('networkx') else 300), 'Origin command/cap differs')
    if row['definition_status'] == 'failed':
        require_final_failure(row,failures,definition)
        return row, {}
    require(row['exit_code'] == 0 and not failures and row['timing_scoring_permitted'] is True and output_name in row['raw_sha256'], 'Origin completion invalid')
    raw = doc(contained(DATA, output_name))
    benchmarks = summary.benchmark_map(raw)
    expected = ledger['binding']['expected_subtest_samples']
    require(set(benchmarks) == set(ledger['binding']['expected_subtests'][definition]), 'Origin subtests differ')
    means = {}
    for name, benchmark in benchmarks.items():
        values = summary.values(benchmark)
        unit = {**raw.get('metadata', {}), **benchmark.get('metadata', {})}.get('unit')
        require(unit == expected[name]['unit'] and len(values) == expected[name]['scored_value_count']
            and all(math.isfinite(v) and v > 0 for v in values), 'Origin scored count/unit/value differs')
        means[name] = statistics.fmean(values)
    require(row['verified_sample_population'] == {name: expected[name] for name in benchmarks}, 'Origin sample proof differs')
    return row, means


def authenticate_original(path, expected, base, report, summary):
    path = Path(path).resolve(strict=True)
    require(path.is_relative_to(DATA.resolve()) and digest(path) == expected, 'Original terminal ledger hash differs')
    ledger = doc(path)
    require(ledger['terminal'] and ledger['protocol'] == base.PROTOCOL and ledger['binding']['protocol'] == base.PROTOCOL
        and ledger['binding']['controller_sha256'] == PRODUCER_SHA and ledger['binding']['head'] == report.HEAD
        and ledger['binding']['receipt_hashes'] == report.RECEIPTS and ledger['binding_digest'] == mdigest(ledger['binding'])
        and ledger['max_child_attempts_per_definition'] == 3 and identity(ledger['terminal_identity'], ledger), 'Original terminal lineage/identity invalid')
    binding = ledger['binding']
    require(len(binding['source_sha256']) == 143 and len(binding['release_sha256']) == 178 and len(binding['baseline_sha256']) == 177, 'Original maps differ')
    require(binding['canonical_sha256'] == report.CANONICAL_SHA and ledger['historical_cpython']['sha256'] == report.CP_HASHES,
        'Original canonical/reference identity differs')
    inputs = {str(path): expected, str(PRODUCER): PRODUCER_SHA, str(REPORT): REPORT_SHA}
    # These installed primary sources were already in v1's complete manager
    # pyperformance tree. Exact hashes justify only the new named-error forms.
    for name, expected_sha in PARSER_SOURCE_SHA.items():
        p = Path(name).resolve(strict=True)
        require(ledger['pins'].get(str(p)) == expected_sha and digest(p) == expected_sha, 'Primary parser source changed/was not originally pinned')
        inputs[str(p)] = expected_sha
    receipts = {}
    for label, sha in report.RECEIPTS.items():
        p = Path(binding['receipt_paths'][label]).resolve(strict=True)
        require(p.is_relative_to(ROOT.resolve()) and digest(p) == sha, 'Accepted origin receipt changed')
        inputs[str(p)] = sha
        receipts[label] = doc(p)
    app, build, correct, perf, accepted = (receipts[k] for k in ('application','build','correctness','performance','accepted_manifest'))
    require(binding['source_sha256'] == app['source_sha256'] == build['source_sha256'] == correct['source_sha256'] == accepted['source_sha256']
        and binding['release_sha256'] == build['release_sha256'] == correct['release_sha256'] == accepted['release_sha256']
        and binding['baseline_sha256'] == app['fixed_baseline_sha256'] == accepted['fixed_baseline_sha256'], 'Accepted origin maps differ')
    require(build['terminal'] and build['passed'] and build['exit_code'] == 0 and correct['correctness_passed']
        and perf['terminal'] and perf['hashes_unchanged'] and perf['status'] == 'gate_passed_original_gc_completed'
        and accepted['terminal'] and accepted['full_validated'] and accepted['fixed_gate_passed'], 'Accepted origin prerequisites differ')
    cpmeans = reference(report, summary, binding)
    for suffix, sha in report.CP_HASHES.items():
        inputs[str(DATA/(report.CP_STEM+suffix))] = sha
    inputs[str(report.SUMMARY)] = report.SUMMARY_SHA
    inputs[str(report.CANONICAL)] = report.CANONICAL_SHA
    require([c['definition'] for c in ledger['cases']] == binding['definitions'], 'Original case list differs')
    results, completed, failed, retained_count = {}, 0, [], 0
    for case in ledger['cases']:
        retained, launches, history = None, 0, []
        ids = [e['attempt_id'] for e in case['attempts']]
        require(len(ids) == len(set(ids)), 'Duplicate original attempt')
        for entry in case['attempts']:
            require(entry['state'] == 'finished', 'Unfinished original requires root recovery')
            row, means = attempt(entry, case, ledger, base, summary, inputs)
            launches += bool(row.get('child_launched'))
            history.append({'origin_id': 'original-v1', **entry})
            if row['valid']:
                require(case['retained_attempt'] == row['attempt_id'] and retained is None and case['status'] == row['definition_status'], 'Original valid association differs')
                retained = {'row': row, 'means': means, 'entry': entry}
        require(launches <= 3 and (case['retained_attempt'] is not None) == (retained is not None), 'Original retention/budget differs')
        if retained:
            retained_count += 1
            if retained['row']['definition_status'] == 'completed': completed += 1
            else: failed.append(case['definition'])
        else:
            require(case['status'] in ('pending', 'invalid_pending', 'invalid_exhausted'), 'Unretained original status differs')
        results[case['definition']] = {'retained': retained, 'history': history, 'launches': launches}
    require(ledger['valid_finished_definitions'] == retained_count and ledger['completed_definitions'] == completed
        and ledger['failed_definitions'] == failed and ledger['capture_complete'] == (retained_count == 97)
        and ledger['suite_passed'] == (retained_count == 97 and not failed), 'Original terminal totals differ')
    return ledger, results, inputs, cpmeans


def authenticate_composite(ledger, original, results, base, summary, inputs):
    require(ledger['protocol'] == ledger['binding']['protocol'] == PROTOCOL and ledger['prefix'] == PREFIX
        and ledger['binding_digest'] == mdigest(ledger['binding']) and ledger['max_child_attempts_per_definition'] == 3
        and [c['definition'] for c in ledger['cases']] == original['binding']['definitions'], 'Supplement protocol/cases differ')
    require(ledger['binding']['parser_policy'] == PARSER_POLICY and ledger['binding']['parser_source_sha256'] == PARSER_SOURCE_SHA,
        'Prospective parser policy/source identity differs')
    for field in ('head', 'receipt_hashes', 'receipt_paths', 'canonical_sha256', 'source_sha256', 'release_sha256', 'baseline_sha256',
            'definitions', 'expected_subtests', 'expected_subtest_samples'):
        require(ledger['binding'][field] == original['binding'][field], 'Supplement engine/workload binding differs: '+field)
    require(ledger['input_trees'] == original['input_trees'] and ledger['original']['binding_digest'] == original['binding_digest']
        and ledger['original']['pins_digest'] == mdigest(original['pins'])
        and ledger['binding']['original_terminal_ledger_sha256'] == ledger['original']['sha256'], 'Original map/tree lineage differs')
    require(all(ledger['pins'].get(p) == h for p,h in original['pins'].items()), 'Original pin changed or omitted')
    require({p:h for p,h in ledger['pins'].items() if p not in original['pins']} == ledger['added_pin_sha256'], 'Undeclared new pins')
    final = {}
    for case in ledger['cases']:
        old = results[case['definition']]
        require(case['prior_attempts'] == old['history'], 'Inherited history changed/reset')
        inherited = old['retained']
        new_retained, launches, ids = None, old['launches'], set(e['attempt_id'] for e in old['history'])
        for entry in case['attempts']:
            require(entry['origin_id'] == 'supplement-v2' and entry['attempt_id'] not in ids and entry['state'] == 'finished', 'New origin association/unfinished differs')
            ids.add(entry['attempt_id'])
            row, means = attempt(entry, case, ledger, base, summary, inputs)
            launches += bool(row.get('child_launched'))
            require(inherited is None, 'Repeated original final case')
            if row['valid']:
                require(new_retained is None and case['retained_origin'] == 'supplement-v2'
                    and case['retained_attempt'] == row['attempt_id'] and case['status'] == row['definition_status'], 'Supplement retained association differs')
                new_retained = {'row': row, 'means': means, 'entry': entry}
        require(launches <= 3, 'Global launch cap exceeded')
        if inherited:
            require(not case['attempts'] and case['retained_origin'] == 'original-v1'
                and case['retained_attempt'] == inherited['row']['attempt_id'] and case['status'] == inherited['row']['definition_status'], 'Original retained outcome changed')
        elif new_retained is None:
            require(case['retained_attempt'] is None and case['retained_origin'] is None
                and case['status'] in ('pending', 'invalid_pending', 'invalid_exhausted'), 'Unretained supplement status differs')
            require((case['status'] == 'invalid_exhausted') == (launches >= 3), 'Global exhausted budget status differs')
        final[case['definition']] = {'retained': inherited or new_retained, 'origin_id': case['retained_origin'], 'launches': launches}
    return final
