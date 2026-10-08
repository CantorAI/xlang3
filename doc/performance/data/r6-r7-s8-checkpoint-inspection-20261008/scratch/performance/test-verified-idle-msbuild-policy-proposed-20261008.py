"""Synthetic policy unit cases only; no OS scan or benchmark is executed."""
import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

HERE = Path(__file__).resolve().parent
HELPER = HERE / 'verified-idle-msbuild-policy-proposed-20261008.py'
spec = importlib.util.spec_from_file_location('quiet_worker_policy', HELPER)
policy_module = importlib.util.module_from_spec(spec); spec.loader.exec_module(policy_module)


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert sys.version_info[:3] == (3, 14, 7) and Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
    assert sys.flags.optimize == 0 and not args.output.exists()
    worker = dict(Name='MSBuild.exe', ProcessId=30756, ParentProcessId=22100,
        CreationDate='2026-10-08T20:23:40.0858180Z', ExecutablePath='C:/VS/MSBuild.exe',
        CommandLine='C:/VS/MSBuild.exe /nodemode:1 /nodeReuse:true', KernelModeTime=2656250, UserModeTime=5312500)
    console = dict(Name='conhost.exe', ProcessId=9220, ParentProcessId=30756,
        CreationDate='2026-10-08T20:23:40.1065150Z', ExecutablePath='C:/Windows/System32/conhost.exe',
        CommandLine='conhost 0x4', KernelModeTime=0, UserModeTime=156250)
    parent = dict(Name='devenv.exe', ProcessId=22100, ParentProcessId=4,
        CreationDate='2026-10-08T20:23:02.9230580Z', ExecutablePath='C:/VS/devenv.exe', CommandLine='devenv')
    worker_proof = dict(worker=worker, parent=parent, direct_children=[dict(console)])
    console_proof = dict(console=console, children=[], worker_ticks={key: worker[key] for key in policy_module.CPU_FIELDS})
    policy = policy_module.policy_from_proofs(worker_proof, console_proof)
    identities = {str(item['ProcessId']): dict(item) for item in (worker, console, parent)}
    snapshot = dict(processes=[{key:item[key] for key in ('Name','ProcessId','ParentProcessId')} for item in (worker,console,parent)] +
        [dict(Name='python.exe', ProcessId=900, ParentProcessId=4)], identities=identities)
    outcomes = []
    def check(name, change, expected, fragment=None):
        actual = copy.deepcopy(snapshot); change(actual)
        result = policy_module.evaluate(actual, policy, 900)
        assert result['valid'] is expected, (name, result)
        if fragment: assert any(fragment in item for item in result['failures']), (name, result)
        outcomes.append(dict(case=name, passed=True, expected_valid=expected))
    def field(role, name, value):
        return lambda s: s['identities'][str(policy[role]['ProcessId'])].__setitem__(name, value)
    check('verified-idle-allowed', lambda s: None, True)
    check('own-timed-subtree-allowed', lambda s:s['processes'].extend([
        dict(Name='python.exe',ProcessId=901,ParentProcessId=900),dict(Name='xlang3.exe',ProcessId=902,ParentProcessId=901)]), True)
    for role in ('worker','console'):
        for name in policy_module.CPU_FIELDS:
            check(role+'-'+name+'-activity', field(role,name,policy[role][name]+1), False, role+'-cpu-')
    check('same-sum-counter-change-rejected', lambda s:s['identities']['30756'].update(KernelModeTime=2656251,UserModeTime=5312499), False,'worker-cpu-')
    for role in ('worker','console','parent'):
        check(role+'-pid-reuse-creation', field(role,'CreationDate','2026-10-08T21:00:00.0000000Z'), False,role+'-identity-')
        check(role+'-missing', lambda s,r=role:s['identities'].pop(str(policy[r]['ProcessId'])),False,role+'-missing')
    check('worker-command-changed',field('worker','CommandLine','different /nodemode:1 /nodeReuse:true'),False,'worker-identity-CommandLine')
    check('worker-executable-changed',field('worker','ExecutablePath','C:/Other/MSBuild.exe'),False,'worker-identity-ExecutablePath')
    check('console-command-changed',field('console','CommandLine','conhost changed'),False,'console-identity-CommandLine')
    check('integer-string-counters-allowed',lambda s:s['identities']['30756'].update(KernelModeTime='2656250',UserModeTime='5312500'),True)
    check('float-counter-rejected',field('worker','KernelModeTime',2656250.0),False,'noninteger')
    check('bool-counter-rejected',field('console','KernelModeTime',False),False,'Boolean')
    check('unknown-direct-child',lambda s:s['processes'].append(dict(Name='conhost.exe',ProcessId=903,ParentProcessId=30756)),False,'unknown-worker-descendant')
    check('unknown-grandchild',lambda s:s['processes'].append(dict(Name='cmd.exe',ProcessId=903,ParentProcessId=9220)),False,'descendant')
    check('unrelated-compiler',lambda s:s['processes'].append(dict(Name='cl.exe',ProcessId=903,ParentProcessId=4)),False,'other-build-')
    check('other-reusable-worker-not-allowed',lambda s:s['processes'].append(dict(Name='MSBuild.exe',ProcessId=903,ParentProcessId=22100)),False,'other-build-')
    check('unrelated-runtime',lambda s:s['processes'].append(dict(Name='python.exe',ProcessId=903,ParentProcessId=4)),False,'other-runtime-')
    check('duplicate-pid',lambda s:s['processes'].append(dict(Name='MSBuild.exe',ProcessId=30756,ParentProcessId=22100)),False,'duplicate-process-id')
    check('scanner-failure',lambda s:s.update(error='WMI unavailable'),False,'scanner-failure')
    sha = lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
    record = dict(status='terminal_policy_tests_passed',terminal=True,cpython_version=sys.version,
        helper_sha256=sha(HELPER),test_source_sha256=sha(Path(__file__)),test_count=len(outcomes),cases=outcomes,
        scope='Pure synthetic process snapshots; no OS scan, subprocess, runtime fixture or benchmark')
    args.output.write_bytes((json.dumps(record,indent=2)+'\n').encode('utf-8'))
    print(record['status'],record['test_count'])


if __name__ == '__main__': main()
