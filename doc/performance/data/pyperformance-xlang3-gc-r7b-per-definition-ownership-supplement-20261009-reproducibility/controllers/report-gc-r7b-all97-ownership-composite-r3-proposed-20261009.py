"""Origin-aware file-only report; no benchmarks or retrospective admission."""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re
import statistics
import sys

ROOT = Path(__file__).resolve().parents[2]
PRODUCER = ROOT/'scratch/performance/run-gc-r7b-all97-ownership-supplement-r3-proposed-20261009.py'


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def require(condition, message):
    if not condition: raise RuntimeError(message)


def main():
    require(not sys.flags.optimize and sys.version_info[:3] == (3,14,7)
        and Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve(), 'Use unoptimized fixed CPython3.14.7')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ledger', type=Path, required=True)
    parser.add_argument('--ledger-sha256', required=True)
    parser.add_argument('--bundle', type=Path, required=True)
    parser.add_argument('--bundle-sha256', required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    require(all(re.fullmatch(r'[0-9a-f]{64}', v) for v in (args.ledger_sha256,args.bundle_sha256)), 'Invalid exact hashes')
    bundle_path = args.bundle.resolve(strict=True)
    require(bundle_path.is_relative_to((ROOT/'scratch/performance').resolve()) and sha(bundle_path) == args.bundle_sha256, 'Bundle proof differs')
    bundle = json.loads(bundle_path.read_text(encoding='utf-8-sig'))
    psha = bundle['file_sha256'][PRODUCER.relative_to(ROOT).as_posix()]
    require(sha(PRODUCER) == psha and sha(__file__) == bundle['file_sha256'][Path(__file__).resolve().relative_to(ROOT).as_posix()], 'Report/producer changed')
    spec = importlib.util.spec_from_file_location('composite_producer', PRODUCER)
    producer = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(producer)
    common, bundle, bundle_path = producer.load_common(bundle_path,args.bundle_sha256)
    base, report, summary = common.primitives()
    path = args.ledger.resolve(strict=True)
    require(path.is_relative_to(common.DATA.resolve()) and common.digest(path) == args.ledger_sha256, 'Composite ledger hash/path differs')
    ledger = common.doc(path)
    require(ledger['terminal'] and ledger['binding']['controller_sha256'] == psha
        and ledger['binding']['bundle_sha256'] == args.bundle_sha256, 'Nonterminal/different producer')
    original, old_results, inputs, cpmeans = common.authenticate_original(ledger['original']['path'],
        ledger['original']['sha256'],base,report,summary)
    require(ledger['original']['controller_sha256'] == common.PRODUCER_SHA, 'Original producer differs')
    added = {p:h for p,h in inputs.items() if p not in original['pins']}
    for name,h in bundle['file_sha256'].items(): added[str((ROOT/name).resolve())] = h
    added[str(bundle_path)] = args.bundle_sha256
    claim_path = common.contained(common.DATA,ledger['exclusive_claim'])
    require(common.digest(claim_path) == ledger['exclusive_claim_sha256'], 'Exclusive reservation changed')
    claim = {'protocol': common.PROTOCOL,'prefix':common.PREFIX,'original':ledger['original'],
        'new_controller_sha256':psha,'bundle_sha256':args.bundle_sha256,'binding_digest':ledger['binding_digest']}
    require(common.doc(claim_path) == claim and ledger['exclusive_claim'] == common.PREFIX+'-exclusive-origin-claim.json', 'Reservation association differs')
    added[str(claim_path)] = ledger['exclusive_claim_sha256']
    require(ledger['added_pin_sha256'] == added and ledger['pins'] == {**original['pins'],**added}, 'Undeclared origin/artifact pin differences')
    final = common.authenticate_composite(ledger,original,old_results,base,summary,inputs)
    inputs.update(added)
    inputs[str(path)] = args.ledger_sha256
    statuses, subtests, attempts, comparisons = [], [], [], []
    completed, failed, invalid = 0, [], 0
    for case in ledger['cases']:
        name = case['definition']; info = final[name]; retained = info['retained']
        all_history = case['prior_attempts']+case['attempts']
        label = case['status'] if all_history else 'not_run'
        means, detail = {}, ''
        if retained:
            if retained['row']['definition_status'] == 'completed':
                completed += 1; means = retained['means']
            else:
                failed.append(name); detail = retained['row'].get('failure_detail') or retained['row'].get('failure','')
        for entry in all_history:
            origin = original if entry['origin_id'] == 'original-v1' else ledger
            row = common.doc(common.contained(common.DATA,entry['receipt']))
            invalid += not row['valid']
            attempts.append({'benchmark':name,'origin':entry['origin_id'],'attempt_id':entry['attempt_id'],
                'valid':row['valid'],'status':row['definition_status'],'child_launched':row.get('child_launched',False),
                'retained':case['retained_origin'] == entry['origin_id'] and case['retained_attempt'] == entry['attempt_id'],
                'origin_binding_digest':origin['binding_digest'],'origin_expected_pin_digest':common.mdigest(origin['pins']),
                'receipt':entry['receipt'],'receipt_sha256':entry['sha256'],
                'reason':row.get('failure_detail') or row.get('error') or row.get('classification_error') or '',
                'timing_scoring_permitted':row['timing_scoring_permitted']})
        xtext = []
        for sub in original['binding']['expected_subtests'][name]:
            x = means.get(sub); ratio = cpmeans[sub]/x if x is not None else None
            if ratio is not None:
                require(math.isfinite(ratio) and ratio > 0,'Nonfinite score')
                comparisons.append({'benchmark':name,'subtest':sub,'speedup':ratio})
                xtext.append(sub+'='+summary.timing(x))
            subtests.append({'benchmark':name,'subtest':sub,'origin':info['origin_id'] or '', 'XLang3 status':label,
                'CPython 3.14.7 historical seconds':cpmeans[sub],'XLang3 seconds':x if x is not None else '',
                'CPython / XLang3 speedup':ratio if ratio is not None else '',
                'XLang3 / CPython elapsed factor':x/cpmeans[sub] if x is not None else '',
                'unit':'second','scored values':original['binding']['expected_subtest_samples'][sub]['scored_value_count'] if x is not None else '',
                'score':'scored historical unpaired' if x is not None else 'unscored'})
        statuses.append({'benchmark':name,'XLang3 status':label,'retained origin':info['origin_id'] or '',
            'XLang3 subtests':'; '.join(xtext),'failure detail':detail,'global child launches':info['launches'],
            'recorded attempts':len(all_history),'retained attempt':case['retained_attempt'] or '',
            'CPython status':'historical October7 completed'})
    count = sum(v['retained'] is not None for v in final.values())
    terminal_ok = bool(common.identity(ledger['terminal_identity'],ledger))
    require(len(statuses) == 97 and len(subtests) == 124 and ledger['valid_finished_definitions'] == count
        and ledger['completed_definitions'] == completed and ledger['failed_definitions'] == failed
        and ledger['capture_complete'] == (count == 97 and terminal_ok)
        and ledger['suite_passed'] == (ledger['capture_complete'] and not failed), 'Composite terminal totals differ')
    counts = {'completed':completed,'failed':len(failed),'unfinished':97-count,'invalid_attempts':invalid,'scored_subtests':len(comparisons)}
    for p,h in inputs.items(): require(common.digest(p) == h,'Report input changed')
    output = args.output_dir.resolve()
    require(output.is_relative_to(common.SCRATCH.resolve()) and output != common.SCRATCH.resolve() and not output.exists(),'Fresh scratch preview required')
    output.mkdir(parents=True,exist_ok=False)
    names = {k:common.PREFIX+s for k,s in (('status','-all-97-status.csv'),('subtests','-all-124-subtests.csv'),
        ('attempts','-all-attempts.csv'),('chart','-speedup.svg'),('report','.md'),('provenance','-report-provenance.json'))}
    report.write_csv(output/names['status'],list(statuses[0]),statuses)
    report.write_csv(output/names['subtests'],list(subtests[0]),subtests)
    report.write_csv(output/names['attempts'],['benchmark','origin','attempt_id','valid','status','child_launched','retained',
        'origin_binding_digest','origin_expected_pin_digest','receipt','receipt_sha256','reason','timing_scoring_permitted'],attempts)
    report.chart(output/names['chart'],comparisons,counts)
    matrix = '\n'.join('| '+r['benchmark']+' | '+r['subtest']+' | '+summary.timing(r['CPython 3.14.7 historical seconds'])+' | '
        +(summary.timing(r['XLang3 seconds']) if r['XLang3 seconds'] != '' else '')+' | '
        +(f"{r['CPython / XLang3 speedup']:.6f}×" if r['CPython / XLang3 speedup'] != '' else '')+' | '
        +(f"{r['XLang3 / CPython elapsed factor']:.6f}×" if r['XLang3 / CPython elapsed factor'] != '' else '')+' | '
        +r['XLang3 status']+' | '+r['origin']+' | '+r['score']+' |' for r in subtests)
    cases = '\n'.join('| '+r['benchmark']+' | '+r['XLang3 status']+' | '+r['retained origin']+' | '
        +str(r['global child launches'])+' | '+r['failure detail'].replace('|','\\|').replace('\n',' ')+' |' for r in statuses)
    geomean = math.exp(statistics.fmean(math.log(c['speedup']) for c in comparisons)) if comparisons else None
    text = f'''# Generic-GC R7b origin-aware per-definition capture

{completed}/97 definitions completed; {len(failed)} genuine final failures; {97-count} unfinished. Capture complete: `{ledger['capture_complete']}`; suite passed: `{ledger['suite_passed']}`. {len(comparisons)} authenticated completed subtests are scored against October7 historical CPython3.14.7 fast results; this is unpaired evidence. Completed-subset geometric mean CP/X: `{geomean}`. CP/X above1 means XLang3 faster; reciprocal X/CP above1 means XLang3 took longer. Invalid/failed/unrun outputs are unscored.

![Historical CP time divided by current XLang3 time]({names['chart']})

[97 statuses](data/{names['status']}), [124 expected subtests](data/{names['subtests']}), [all attempts](data/{names['attempts']}) and [authentication](data/{names['provenance']}). Original ledger SHA `{ledger['original']['sha256']}`; composite SHA `{args.ledger_sha256}`.

## Distinct origins and limits

Original-v1 uses frozen48faf, its original parser and full pin-map proof. Supplement-v2 uses its separately frozen R3 producer/binding, prospective older-row ownership correction and strict single-definition timeout/death recognition. The primary pyperformance1.14.0 run.py/commands.py hashes and parser policy are pinned in the binding. Every retained receipt is checked against its own origin digest/parser, never relabelled as the other origin. All {invalid} invalid attempts remain excluded, even where a later valid outcome exists. Valid original completions and genuine failures were never repeated. The cumulative child-launch cap is3 per definition across both origins. The fixed exclusive reservation prevents a fresh-prefix budget reset; crash recovery is independent root work.

Engine143/Release178/baseline177 and original dependency/workload pins are identical across origins; added pins enumerate immutable origin evidence and sibling/bundle/reservation artifacts. The original runner, fast mode,300-second complete-case cap/networkx600, bodies/datasets/calibration/values and shared watcher are unchanged. Per-definition windows, explicit resumptions and two observation-policy versions differ from the historical single-manager CP protocol. Host/order/cache differences remain possible. Historical dependency METADATA does not prove every old transitive/data/native byte. Source143 includes six unowned dirty inputs, not a clean-checkout claim.

Foreign runtimes/tools and malformed creation still invalidate. Dormant-worker parent guard is unchanged. The prospective parser requires one exact canonical[1/1]header; malformed/multiple/mismatched headers or duplicate/contradictory known markers raise an invalid-log error. A same-name legacy footer may have a matching official timeout/death marker without a no-suite line; named-only failure requires exactly one No-benchmark-was-run line. Final failure requires nonzero exit and unchanged phase guards. Exit0/claimed-completed with a failure marker is invalid. Partial means in failed logs are retained but never scored. One-second polling plus scan time can miss short foreign processes; unseen orphan descendants fail closed. Raw invalid observations are not retrospectively recertified. Terminal composite identity passed: `{terminal_ok}`. This artifact audit does not load binaries, rerun benchmarks, revalidate the engine currently on disk or prove each benchmark's semantics exhaustively. No causal engine-gain/universal CPython-win claim is made.

## Every original definition

| Definition | XLang3 status | Retained origin | Global child launches | Failure detail |
| --- | --- | --- | ---: | --- |
{cases}

## Complete124 expected-subtest comparison matrix

Times are arithmetic scored means. CP/X speed above1 means X faster; X/CP elapsed factor above1 means X slower. Unsuccessful X time/ratios are blank.

| Definition | Subtest | Historical CP3.14.7 time | XLang3 time | CP/X speed (>1 X faster) | X/CP elapsed (>1 X slower) | Status | Origin | Score |
| --- | --- | ---: | ---: | ---: | ---: | --- | --- | --- |
{matrix}
'''
    with (output/names['report']).open('x',encoding='utf-8',newline='\n') as stream: stream.write(text)
    report.write_json(output/names['provenance'],{'status':'authenticated_origin_aware_preview','created_utc':datetime.now(timezone.utc).isoformat(),
        'original':ledger['original'],'composite_ledger':str(path),'composite_sha256':args.ledger_sha256,'new_binding':ledger['binding'],
        'original_binding':original['binding'],'original_pin_digest':common.mdigest(original['pins']),
        'supplement_pin_digest':common.mdigest(ledger['pins']),'input_sha256':inputs,'counts':counts,
        'capture_complete':ledger['capture_complete'],'suite_passed':ledger['suite_passed'],'historical_unpaired':True})
    for p,h in inputs.items(): require(common.digest(p) == h,'Evidence changed during rendering')
    files = [{'source':(output/n).relative_to(ROOT).as_posix(),'destination':
        ('doc/performance/' if k in ('chart','report') else 'doc/performance/data/')+n,
        'sha256':sha(output/n),'bytes':(output/n).stat().st_size} for k,n in names.items()]
    manifest = output/(common.PREFIX+'-report-preview-manifest.json')
    report.write_json(manifest,{'status':'verified_preview_only_not_published','files':files,'file_count':len(files),
        'input_hashes_unchanged':True,'original_ledger_sha256':ledger['original']['sha256'],'composite_ledger_sha256':args.ledger_sha256})
    print(json.dumps({'manifest':str(manifest),'sha256':sha(manifest),'counts':counts},indent=2),flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
