"""Root-only in-memory ownership tests; never scans or launches a process."""
from __future__ import annotations
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
from types import SimpleNamespace
import sys

ROOT = Path(__file__).resolve().parents[2]
PRODUCER = ROOT/'scratch/performance/run-gc-r7b-all97-ownership-supplement-r3-proposed-20261009.py'


def sha(path):
    with Path(path).open('rb') as stream: return hashlib.file_digest(stream,'sha256').hexdigest()


def require(condition,message):
    if not condition: raise RuntimeError(message)


def main():
    require(not sys.flags.optimize and sys.version_info[:3] == (3,14,7)
        and Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve(),'Use fixed unoptimized CPython3.14.7')
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bundle',type=Path,required=True)
    parser.add_argument('--bundle-sha256',required=True)
    parser.add_argument('--original-ledger',type=Path,required=True)
    parser.add_argument('--original-ledger-sha256',required=True)
    args = parser.parse_args()
    require(sha(args.bundle) == args.bundle_sha256 and sha(args.original_ledger) == args.original_ledger_sha256,
        'Exact bundle/original ledger hash differs')
    require(json.loads(args.original_ledger.read_text(encoding='utf-8-sig'))['terminal'],'Tests wait for original terminal')
    bundle = json.loads(args.bundle.read_text(encoding='utf-8-sig'))
    require(sha(PRODUCER) == bundle['file_sha256'][PRODUCER.relative_to(ROOT).as_posix()],'Producer changed')
    spec = importlib.util.spec_from_file_location('synthetic_producer',PRODUCER)
    producer = importlib.util.module_from_spec(spec); spec.loader.exec_module(producer)
    common, _, _ = producer.load_common(args.bundle,args.bundle_sha256)
    base, _, summary = common.primitives()
    shared = common.load(base.WATCH,base.WATCH_SHA,'synthetic_shared_watcher')
    manager = {'Name':'python.exe','ProcessId':os.getpid(),'ParentProcessId':0,
        'CreationDate':'2026-01-02T00:00:00Z','KernelModeTime':0,'UserModeTime':0,'CommandLine':''}
    pidbase = manager['ProcessId'] + 1000000
    def row(name,pid,creation,parent=None):
        return {'Name':name,'ProcessId':pid,'ParentProcessId':manager['ProcessId'] if parent is None else parent,
            'CreationDate':creation,'KernelModeTime':0,'UserModeTime':0,'CommandLine':'private-unrelated-command'}
    base.OwnedProcesses = producer.ownership_class(base)
    def factory():
        fake = SimpleNamespace(scan=lambda:[manager],classify=shared.classify)
        old_loader = base.load_module
        try:
            base.load_module = lambda path,name:fake
            return base.make_watcher('synthetic',1,None,None)[1::2]
        finally:
            base.load_module = old_loader
    # factory returns (owned, classifier), exercising the actual unchanged
    # ledger-local runtime wrapper while fake.scan reads only synthetic rows.
    old = '2026-01-01T00:00:00Z'; new = '2026-01-03T00:00:00Z'
    owned,classify = factory(); older = row('unrelated.exe',pidbase+1,old)
    require(not classify([manager,older])['busy'] and (pidbase+1,old) not in owned.known,'Older nonruntime falsely owned/refused')
    print('PASS older nonruntime remains unowned')
    for name in ('python.exe','xlang3.exe'):
        owned,classify = factory(); older = row(name,pidbase+2,old)
        require(classify([manager,older])['busy'] and (pidbase+2,old) not in owned.known,'Older foreign runtime admitted')
    print('PASS older foreign runtimes remain busy')
    for name in ('cl.exe','ctest.exe'):
        owned,classify = factory(); older = row(name,pidbase+3,old)
        require(classify([manager,older])['busy'] and (pidbase+3,old) not in owned.known,'Older tool escaped original classifier')
    print('PASS older tools remain busy')
    for creation in (manager['CreationDate'],new):
        owned,classify = factory(); child = row('python.exe',pidbase+4,creation)
        require(not classify([manager,child])['busy'] and (pidbase+4,creation) in owned.known,'Equal/newer child not creation-pinned')
    print('PASS equal and newer children remain pinned')
    owned,classify = factory(); child = row('python.exe',pidbase+5,new); grand = row('xlang3.exe',pidbase+6,new,pidbase+5)
    require(not classify([manager,grand,child])['busy'] and (pidbase+6,new) in owned.known,'Transitive child admission changed')
    print('PASS transitive observed descendants')
    owned,classify = factory(); orphan = row('python.exe',pidbase+7,new,(manager['ProcessId']+9999999))
    require(classify([manager,orphan])['busy'] and (pidbase+7,new) not in owned.known,'Unseen orphan admitted')
    print('PASS unseen orphan stays foreign')
    for bad in (None,'malformed'):
        owned,classify = factory()
        try: classify([manager,row('unrelated.exe',pidbase+8,bad)])
        except RuntimeError as error:
            require('private-unrelated-command' not in str(error) and 'matched_parent' in str(error),'Refusal context privacy failed')
        else: raise RuntimeError('Missing/malformed creation failed open')
    print('PASS malformed creation fails closed with private identity context')
    owned,classify = factory()
    try: classify([{**manager,'CreationDate':new}])
    except RuntimeError: pass
    else: raise RuntimeError('Manager lifetime reuse admitted')
    print('PASS manager lifetime replacement refused')
    owned,classify = factory(); child = row('python.exe',pidbase+9,new)
    require(not classify([manager,child])['busy'],'Initial child not admitted')
    reused = row('python.exe',pidbase+9,'2026-01-04T00:00:00Z',(manager['ProcessId']+9999999))
    require(classify([manager,reused])['busy'],'Recorded child PID admitted a different lifetime')
    print('PASS child PID lifetime replacement stays foreign')
    owned,classify = factory(); tool = row('cl.exe',pidbase+10,new)
    require(classify([manager,tool])['busy'],'Owned-name tool escaped strict tool classification')
    print('PASS all guarded tools remain busy even under owned parents')
    require(producer.launch_count(1,[{'child_launched':True},{'child_launched':False},{'child_launched':True}]) == 3
        and producer.launch_count(2,[{'child_launched':False}]) == 2,'Global launch aggregation/no-child cost wrong')
    print('PASS inherited launch counts and zero-cost preflight')
    definition = json.loads(args.original_ledger.read_text(encoding='utf-8-sig'))['binding']['definitions'][0]
    header = '[1/1] '+definition+'...\n'
    timeout = 'ERROR: Benchmark '+definition+' timed out\nERROR: No benchmark was run\n'
    death = 'ERROR: Benchmark '+definition+' failed: Benchmark died\nERROR: No benchmark was run\n'
    for text,reason in ((header+timeout,'Benchmark timed out'),(header+death,'Benchmark died')):
        failures,_ = common.prospective_failure_details(summary,text,definition)
        require(failures == {definition:reason},'Exact named official failure not recognized')
        common.require_final_failure({'definition_status':'failed','exit_code':1,
            'partial_timings_never_scored':True,'timing_scoring_permitted':False},failures,definition)
    print('PASS exact named official timeout and death failures')
    def invalid_log(text):
        try:
            common.prospective_failure_details(summary,text,definition)
        except RuntimeError as error:
            require(str(error).startswith('Invalid prospective log: '),'Wrong invalid-log refusal')
        else:
            raise RuntimeError('Invalid log returned the clean-success sentinel')
    mean = definition+': Mean +- std dev: 1 ms +- 0.1 ms\n'
    require(common.prospective_failure_details(summary,header+mean,definition) == ({},{}),
        'Canonical clean success was rejected')
    malformed = (mean, header.replace(definition,'unrelated')+mean, header+header+mean,
        header.replace('[1/1]','[1/2]')+mean, header.replace('[1/1]','[2/1]')+mean,
        header.replace('[1/1]','[1/x]')+mean, header+header.replace('[1/1]','[1/2]')+mean,
        header+header.replace('[1/1]','[1/x]')+mean)
    for text in malformed:
        invalid_log(text)
    print('PASS clean success and malformed success headers distinguished')
    rejected = (timeout, header.replace(definition,'unrelated')+timeout, header+header+timeout,
        header.replace('[1/1]','[1/2]')+timeout, header+'ERROR: unrelated\nERROR: No benchmark was run\n',
        header+'ERROR: Benchmark unrelated timed out\nERROR: No benchmark was run\n',
        header+'ERROR: Benchmark '+definition+' failed: arbitrary exception\nERROR: No benchmark was run\n',
        header+'ERROR: Benchmark '+definition+' timed out\n',
        header+timeout+death, header+timeout+timeout,
        header+timeout+'ERROR: No benchmark was run\n')
    for text in rejected:
        invalid_log(text)
    print('PASS missing mismatched duplicate failure evidence explicitly invalid')
    partial = header+mean+death
    failures,_ = common.prospective_failure_details(summary,partial,definition)
    require(failures == {definition:'Benchmark died'},'Legitimate partial means prevented final failure')
    common.require_final_failure({'definition_status':'failed','exit_code':1,
        'partial_timings_never_scored':True,'timing_scoring_permitted':False},failures,definition)
    for exit_code,status in ((0,'failed'),(0,'completed'),(1,'completed')):
        try:
            common.require_final_failure({'definition_status':status,'exit_code':exit_code,
                'partial_timings_never_scored':True,'timing_scoring_permitted':False},failures,definition)
        except RuntimeError: pass
        else: raise RuntimeError('Success/claimed-completed failure conflict admitted')
    # The frozen actual phase's successful-result guard requires not failures;
    # the exact same parser is injected there, so these markers revoke success.
    require(bool(failures),'Actual completion guard would miss the named failure')
    print('PASS failure partial means unscored and success conflicts rejected')
    footer_death = '- '+definition+' (Benchmark died)\n'
    footer_timeout = '- '+definition+' (Benchmark timed out)\n'
    marker_death = 'ERROR: Benchmark '+definition+' failed: Benchmark died\n'
    marker_timeout = 'ERROR: Benchmark '+definition+' timed out\n'
    legacy = header+footer_death
    require(common.prospective_failure_details(summary,legacy,definition)[0] == {definition:'Benchmark died'},'Frozen footer recognition lost')
    for footer,marker,reason in ((footer_death,marker_death,'Benchmark died'),
            (footer_timeout,marker_timeout,'Benchmark timed out')):
        for no_suite in ('','ERROR: No benchmark was run\n'):
            failures,_ = common.prospective_failure_details(summary,header+mean+marker+footer+no_suite,definition)
            require(failures == {definition:reason},'Coherent footer/marker failed without no-suite')
    print('PASS coherent footer and named marker with failed partial means')
    contradictions = (
        header+footer_death+marker_timeout,
        header+footer_timeout+marker_death,
        header+footer_death+'ERROR: Benchmark unrelated failed: Benchmark died\n',
        header+footer_death+'ERROR: Benchmark unrelated-name failed: Benchmark died\n',
        header+'- unrelated (Benchmark died)\n'+marker_death,
        header+footer_death+footer_death,
        header+footer_death+footer_timeout,
        header+footer_death+marker_death+marker_death,
        header+footer_death+marker_death+marker_timeout,
        header+footer_death+'- unrelated (Benchmark died)\n',
        header+footer_death+marker_death+'ERROR: Benchmark unrelated timed out\n')
    for text in contradictions:
        invalid_log(text)
    print('PASS footer early-return contradictions and duplicates refused')
    require(summary.failure_details(header+death) == ({},{}),'Original-v1 parser was retrospectively changed')
    print('PASS legacy footer retained and original parser unchanged')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
