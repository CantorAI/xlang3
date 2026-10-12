"""Execute the frozen proposed semantic fixture on the required CPython first."""
import ast
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path('D:/CantorAI/xlang3')
CP = Path('C:/Python/Python314/python.exe')
DATA = ROOT/'doc/performance/data'
PROOF = ROOT/'scratch/performance/native-str-utf8-encode-proposed-20261008-provenance.json'
PROOF_SHA = '4fd1a6d4d2b9c6c85f7dcdc0f87a6d633e9fbefb3bc61899c0dda9f48732b570'
PREFIX = 'native-str-utf8-cpython-semantics-20261008'
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
read = lambda p: json.loads(Path(p).read_bytes())

def main():
    assert Path(sys.executable).resolve() == CP.resolve() and sys.version_info[:3] == (3,14,7)
    assert not sys.flags.optimize and sha(PROOF) == PROOF_SHA
    proof = read(PROOF)
    assert proof['status'] == 'frozen_scratch_proposal_unapplied_unexecuted_unmeasured'
    fixture = ROOT/proof['semantic_fixture']['source']
    expected = ROOT/proof['semantic_fixture']['expected']
    assert sha(fixture) == proof['semantic_fixture']['source_sha256']
    assert sha(expected) == proof['semantic_fixture']['expected_sha256']
    ast.parse(fixture.read_bytes(), filename=str(fixture))
    pins = {ROOT/p: value for p,value in proof['raw_before_sha256'].items()}
    pins.update({PROOF:PROOF_SHA, fixture:sha(fixture), expected:sha(expected), CP:sha(CP), CP.with_name('python314.dll'):sha(CP.with_name('python314.dll'))})
    assert all(sha(p) == value for p,value in pins.items())
    output = DATA/(PREFIX+'.json')
    assert not output.exists()
    record = dict(status='running_cpython_semantic_preflight',terminal=False,scored=False,
                  command=[str(CP),'-I','-B','-u',str(fixture)],
                  proposal_sha256=PROOF_SHA,fixture_sha256=sha(fixture),expected_sha256=sha(expected),
                  started_utc=datetime.now(timezone.utc).isoformat())
    save = lambda: output.write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    save()
    stdout,stderr = DATA/(PREFIX+'.stdout.log'),DATA/(PREFIX+'.stderr.log')
    env = dict(os.environ)
    for key in ('PYTHONPATH','PYTHONPYCACHEPREFIX','PYTHONIOENCODING','PYTHONOPTIMIZE'): env.pop(key,None)
    with stdout.open('xb') as out,stderr.open('xb') as err:
        child = subprocess.Popen(record['command'],cwd=ROOT,env=env,stdout=out,stderr=err,
                                 stdin=subprocess.DEVNULL,creationflags=subprocess.CREATE_NO_WINDOW)
        record['pid'] = child.pid
        try: record['exit_code'] = child.wait(timeout=60)
        except subprocess.TimeoutExpired:
            subprocess.run(['taskkill','/F','/T','/PID',str(child.pid)],capture_output=True,timeout=15)
            record['exit_code'] = child.wait(timeout=15)
            record['timeout'] = True
    for stream,path in (('stdout',stdout),('stderr',stderr)):
        record[stream+'_log'],record[stream+'_sha256'] = path.name,sha(path)
    record['hashes_unchanged'] = all(sha(p) == value for p,value in pins.items())
    record['passed'] = record['exit_code'] == 0 and not record.get('timeout') and stderr.read_bytes() == b'' and stdout.read_bytes().replace(b'\r\n',b'\n') == expected.read_bytes().replace(b'\r\n',b'\n') and record['hashes_unchanged']
    record.update(terminal=True,status='cpython_semantics_passed' if record['passed'] else 'cpython_semantics_failed',completed_utc=datetime.now(timezone.utc).isoformat())
    save()
    print(json.dumps({'status':record['status'],'passed':record['passed'],'receipt_sha256':sha(output)}))
    assert record['passed']

if __name__ == '__main__': main()
