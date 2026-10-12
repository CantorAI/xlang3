"""Held single prospective supplement; root execution only after original terminal.

Reuses the frozen v1 phase, watcher and workload. Old outcomes keep their own
origin proofs/parser. Prospective corrections: older-row ownership refusal and
strict single-definition failure recognition with explicit invalid-log refusal.
No body/cap changes.
"""
from __future__ import annotations
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import sys
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
COMMON = ROOT/'scratch/performance/gc-r7b-ownership-supplement-common-r3-proposed-20261009.py'
NEW_FILES = {'scratch/performance/gc-r7b-ownership-supplement-common-r3-proposed-20261009.py',
    'scratch/performance/run-gc-r7b-all97-ownership-supplement-r3-proposed-20261009.py',
    'scratch/performance/report-gc-r7b-all97-ownership-composite-r3-proposed-20261009.py',
    'scratch/performance/check-gc-r7b-ownership-supplement-synthetic-r3-proposed-20261009.py'}


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def load_common(bundle_path, bundle_sha):
    import hashlib
    def sha(path):
        with Path(path).open('rb') as stream:
            return hashlib.file_digest(stream, 'sha256').hexdigest()
    bundle_path = Path(bundle_path).resolve(strict=True)
    require(bundle_path.is_relative_to((ROOT/'scratch/performance').resolve()) and sha(bundle_path) == bundle_sha, 'Bundle proof hash/path differs')
    bundle = json.loads(bundle_path.read_text(encoding='utf-8-sig'))
    require(bundle['status'] == 'prepared_not_executed_original_terminal_required' and set(bundle['file_sha256']) == NEW_FILES,
        'Different sibling bundle')
    for name, expected in bundle['file_sha256'].items():
        require(sha(ROOT/name) == expected, 'Sibling file changed: '+name)
    spec = importlib.util.spec_from_file_location('supplement_common', COMMON)
    common = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(common)
    return common, bundle, bundle_path


def ownership_class(base):
    class ProspectiveOwnedProcesses(base.OwnedProcesses):
        def allowed(self, rows, manager_only=False):
            with self.lock:
                manager = next((p for p in rows if p['ProcessId'] == self.manager['ProcessId']), None)
                require(manager is not None and base.process_identity(manager) == self.manager,
                    'Manager lifetime changed; observed identity='+json.dumps(base.process_identity(manager) if manager else None))
                if manager_only:
                    return {(self.manager['ProcessId'], self.manager['CreationDate'])}
                live = {p['ProcessId']: p for p in rows if (p['ProcessId'], p.get('CreationDate')) in self.known}
                changed = True
                while changed:
                    changed = False
                    for row in rows:
                        key = (row['ProcessId'], row.get('CreationDate'))
                        if key in self.known:
                            continue
                        parent = live.get(row['ParentProcessId'])
                        if parent is None:
                            continue
                        try:
                            older = base.creation_seconds(row['CreationDate']) < base.creation_seconds(parent['CreationDate'])
                        except Exception as error:
                            # Refusal context contains identities only, never unrelated commands.
                            raise RuntimeError('Ownership creation refusal '+json.dumps({'row': base.process_identity(row),
                                'matched_parent': base.process_identity(parent)})+': '+repr(error)) from error
                        if older:
                            # This stored parent PID predates the currently observed lifetime.
                            # Leave it unowned; original tools/foreign runtimes remain busy.
                            continue
                        self.known[key] = base.process_identity(row)
                        live[row['ProcessId']] = row
                        changed = True
                return set(self.known)
    return ProspectiveOwnedProcesses


def launch_count(old_launches, attempts):
    return old_launches + sum(bool(entry.get('child_launched')) for entry in attempts)


def main():
    require(not sys.flags.optimize and sys.version_info[:3] == (3, 14, 7)
        and Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve(), 'Use fixed unoptimized CPython3.14.7')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--original-ledger', type=Path, required=True)
    parser.add_argument('--original-ledger-sha256', required=True)
    parser.add_argument('--bundle', type=Path, required=True)
    parser.add_argument('--bundle-sha256', required=True)
    parser.add_argument('--resume-sha256')
    parser.add_argument('--known-orphan-worker', type=int)
    parser.add_argument('--known-orphan-worker-creation')
    parser.add_argument('--authenticate-only', action='store_true')
    args = parser.parse_args()
    require(all(re.fullmatch(r'[0-9a-f]{64}', v) for v in (args.original_ledger_sha256, args.bundle_sha256)), 'Invalid exact hashes')
    require((args.known_orphan_worker is None) == (args.known_orphan_worker_creation is None)
        and (args.known_orphan_worker is None or args.known_orphan_worker > 0), 'Exact worker PID/creation pair required')
    common, bundle, bundle_path = load_common(args.bundle, args.bundle_sha256)
    base, report, summary = common.primitives()
    original, old_results, origin_inputs, cpmeans = common.authenticate_original(args.original_ledger,
        args.original_ledger_sha256, base, report, summary)
    namespace = argparse.Namespace(head=original['binding']['head'])
    for label in ('application', 'build', 'correctness', 'performance', 'accepted_manifest'):
        setattr(namespace, label, Path(original['binding']['receipt_paths'][label]))
        setattr(namespace, label+'_sha256', original['binding']['receipt_hashes'][label])
    fresh_binding, fresh_pins, trees, _, _ = base.authenticate(namespace)
    require(fresh_binding == original['binding'] and fresh_pins == original['pins'] and trees == original['input_trees'],
        'Accepted engine/dependency/workload bytes or original pin inventory changed')
    added = {p:h for p,h in origin_inputs.items() if p not in fresh_pins}
    for name, sha in bundle['file_sha256'].items(): added[str((ROOT/name).resolve())] = sha
    added[str(bundle_path)] = args.bundle_sha256
    pins = {**fresh_pins, **added}
    require(all(pins.get(p) == h for p,h in fresh_pins.items()), 'Original pins overwritten')
    binding = {**fresh_binding, 'protocol': common.PROTOCOL, 'controller_sha256': common.digest(__file__),
        'original_terminal_ledger_sha256': args.original_ledger_sha256, 'bundle_sha256': args.bundle_sha256,
        'parser_policy': common.PARSER_POLICY, 'parser_source_sha256': common.PARSER_SOURCE_SHA,
        'ownership_policy': 'Skip provably older unknown parent-PID rows from ownership; identity-only refusal context; all other guards unchanged'}
    require(base.stable(pins, trees, binding['head'])['passed'], 'Supplement preflight identity changed')
    if args.authenticate_only:
        print(json.dumps({'status': 'supplement_authentication_passed_no_children', 'original_ledger_sha256': args.original_ledger_sha256,
            'retained_original_definitions': sum(v['retained'] is not None for v in old_results.values()),
            'pending_definitions': [n for n,v in old_results.items() if v['retained'] is None and v['launches'] < 3],
            'exhausted': [n for n,v in old_results.items() if v['retained'] is None and v['launches'] >= 3],
            'binding_digest': common.mdigest(binding), 'prefix': common.PREFIX}, indent=2), flush=True)
        return 0
    path = common.DATA/(common.PREFIX+'-ledger.json')
    claim_path = common.DATA/(common.PREFIX+'-exclusive-origin-claim.json')
    original_record = {'path': str(args.original_ledger.resolve()), 'sha256': args.original_ledger_sha256,
        'controller_sha256': common.PRODUCER_SHA, 'binding_digest': original['binding_digest'], 'pins_digest': common.mdigest(original['pins'])}
    claim = {'protocol': common.PROTOCOL, 'prefix': common.PREFIX, 'original': original_record,
        'new_controller_sha256': binding['controller_sha256'], 'bundle_sha256': args.bundle_sha256, 'binding_digest': common.mdigest(binding)}
    if args.resume_sha256:
        require(re.fullmatch(r'[0-9a-f]{64}', args.resume_sha256) and common.digest(path) == args.resume_sha256, 'Supplement resume ledger hash differs')
        ledger = common.doc(path)
        require(common.doc(claim_path) == claim and common.digest(claim_path) == ledger['exclusive_claim_sha256'], 'Exclusive reservation differs')
        added[str(claim_path.resolve())] = ledger['exclusive_claim_sha256']
        pins[str(claim_path.resolve())] = ledger['exclusive_claim_sha256']
        require(ledger['terminal'] and ledger['original'] == original_record and ledger['binding'] == binding
            and ledger['pins'] == pins and ledger['input_trees'] == trees and ledger['added_pin_sha256'] == added,
            'Supplement resume origin/input binding differs')
        common.authenticate_composite(ledger, original, old_results, base, summary, {})
        require(not ledger['capture_complete'], 'All97 already final; never repeat valid definitions')
    else:
        require(not any(common.DATA.glob(common.PREFIX+'*')), 'Single supplement already reserved; root recovery/resume required')
        # Exclusive immutable claim precedes mutable ledger creation. A crash here
        # intentionally requires root recovery and cannot reset the global budget.
        base.write_attempt(claim_path, claim)
        added[str(claim_path.resolve())] = common.digest(claim_path)
        pins[str(claim_path.resolve())] = common.digest(claim_path)
        cases = []
        for old in original['cases']:
            info = old_results[old['definition']]
            retained = info['retained']
            cases.append({'definition': old['definition'], 'status': old['status'] if retained else
                'invalid_exhausted' if info['launches'] >= 3 else 'pending',
                'retained_attempt': old['retained_attempt'], 'retained_origin': 'original-v1' if retained else None,
                'prior_attempts': info['history'], 'attempts': []})
        ledger = {'protocol': common.PROTOCOL, 'prefix': common.PREFIX, 'created_utc': base.now(), 'binding': binding,
            'binding_digest': common.mdigest(binding), 'pins': pins, 'added_pin_sha256': added, 'input_trees': trees,
            'original': original_record, 'exclusive_claim': claim_path.name, 'exclusive_claim_sha256': common.digest(claim_path),
            'max_child_attempts_per_definition': 3, 'expected_definitions': 97, 'cases': cases, 'sessions': [],
            'capture_complete': False, 'suite_passed': False,
            'scope': 'One original origin and one prospective ownership-corrected supplement. Old invalid timings stay excluded; historical CP is unpaired.'}
    session = {'id': str(len(ledger['sessions'])+1).zfill(3), 'started_utc': base.now(), 'manager_pid': os.getpid(),
        'previous_ledger_sha256': args.resume_sha256, 'known_orphan_worker_pid': args.known_orphan_worker}
    ledger['sessions'].append(session)
    ledger.update(status='running', terminal=False, resumable=False)
    def save():
        for case in ledger['cases']:
            for entry in case['attempts']: entry['origin_id'] = 'supplement-v2'
            if case['retained_attempt'] in {e['attempt_id'] for e in case['attempts']}:
                case['retained_origin'] = 'supplement-v2'
        base.atomic_save(path, ledger)
    save()
    base.OwnedProcesses = ownership_class(base)
    namespace.prefix = common.PREFIX
    namespace.known_orphan_worker = args.known_orphan_worker
    namespace.known_orphan_worker_creation = args.known_orphan_worker_creation
    try:
        for case in ledger['cases']:
            if case['retained_origin'] is not None: continue
            launches = launch_count(old_results[case['definition']]['launches'], case['attempts'])
            if launches >= 3:
                case['status'] = 'invalid_exhausted'; save(); continue
            print('Prospective original definition', case['definition'], 'prior child launches', launches, flush=True)
            # Use exactly the same prospective helper as new-origin receipt
            # authentication. The original summary module/file remains intact.
            selected = case['definition']
            phase_summary = SimpleNamespace(**{**vars(summary), 'failure_details':
                lambda text, definition=selected: common.prospective_failure_details(summary,text,definition)})
            stop, row = base.attempt_case(namespace, ledger, case, pins, trees, phase_summary, save)
            total = launch_count(old_results[case['definition']]['launches'], case['attempts'])
            if not row['valid']: case['status'] = 'invalid_exhausted' if total >= 3 else 'invalid_pending'
            save()
            if stop:
                ledger['stop_reason'] = row.get('error') or row.get('post_guard_error') or 'Post-attempt guard invalid'; break
    except BaseException as error:
        ledger['controller_error'] = repr(error)
    finally:
        retained = [c for c in ledger['cases'] if c['retained_origin'] is not None]
        pending = [c['definition'] for c in ledger['cases'] if c['retained_origin'] is None and c['status'] != 'invalid_exhausted']
        ledger['terminal_identity'] = base.stable(pins, trees, binding['head'])
        ledger.update(terminal=True, capture_complete=len(retained) == 97 and ledger['terminal_identity']['passed'],
            valid_finished_definitions=len(retained), completed_definitions=sum(c['status'] == 'completed' for c in retained),
            failed_definitions=[c['definition'] for c in retained if c['status'] == 'failed'], pending_definitions=pending,
            exhausted_invalid_definitions=[c['definition'] for c in ledger['cases'] if c['status'] == 'invalid_exhausted'],
            resumable=bool(pending) and ledger['terminal_identity']['passed'], finished_utc=base.now())
        ledger['suite_passed'] = ledger['capture_complete'] and not ledger['failed_definitions']
        ledger['status'] = 'complete_all97_composite' if ledger['capture_complete'] else 'resumable_composite_pending' if ledger['resumable'] else 'terminal_composite_incomplete'
        session.update(finished_utc=base.now(), status=ledger['status'])
        save()
    print(json.dumps({'ledger': str(path), 'sha256': common.digest(path), 'status': ledger['status'],
        'completed_definitions': ledger['completed_definitions'], 'failed_definitions': ledger['failed_definitions'],
        'pending_definitions': ledger['pending_definitions'], 'capture_complete': ledger['capture_complete']}, indent=2), flush=True)
    return 0 if ledger['capture_complete'] else 2


if __name__ == '__main__':
    raise SystemExit(main())
