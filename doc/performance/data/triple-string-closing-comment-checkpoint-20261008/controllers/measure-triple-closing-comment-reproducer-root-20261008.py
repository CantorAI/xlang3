"""Untimed CPython/native-parser/current-XLang3 reproduction, original source."""
import ast
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
DATA=ROOT/'doc/performance/data'
CP=Path('C:/Python/Python314/python.exe')
RELEASE=ROOT/'build-repro/main-verify-20261006/Release'
HELPER=ROOT/'scratch/performance/sqlalchemy-direct-native-parser-20261008/direct-parser.exe'
PROOF=ROOT/'scratch/performance/sqlalchemy-triple-closing-comment-r2-provenance-proposed-20261008.json'
OUT=DATA/'sqlalchemy-triple-closing-comment-reproducer-20261008.json'
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
assert Path(sys.executable).resolve()==CP.resolve() and sys.version_info[:3]==(3,14,7)
assert sys.flags.isolated==1 and sys.flags.optimize==0 and not OUT.exists()
assert sha(PROOF)=='44378c66e1c140cbfde0ddbdb0182cfc0000f765ff5a2f0c19aaddeec1656bfb'
proof=json.loads(PROOF.read_bytes())
before={str(ROOT/p):h for p,h in proof['inputs_sha256'].items()}
before.update({str(ROOT/c['path']):c['sha256'] for c in proof['cases']})
for p in [Path(__file__),PROOF,CP,HELPER,RELEASE/'xlang3.exe',RELEASE/'xlang3_runtime.dll']:
    before[str(p)]=sha(p)
assert before[str(HELPER)]=='4b1b6144e4e49afbad3eb0046ccc3b7e6dab9fbf2a9b3882d347625350b416e8'
assert before[str(RELEASE/'xlang3_runtime.dll')]=='64640e503a103341dd44d220eba17ef802219b9df639bb1be5cbe5f2bf847564'
assert all(sha(p)==h for p,h in before.items())
record=dict(status='preflight',terminal=False,diagnostic_only=True,scored=False,engine_changes=False,
    hashes_before=before,phases=[],failure_reproduced=False)
def save():OUT.write_bytes((json.dumps(record,indent=2)+'\n').encode())
env=os.environ.copy();env['PATH']=str(RELEASE)+os.pathsep+env.get('PATH','')
env['XLANG3_PYTHON_LIB']=str(CP.parent/'Lib')
for name in ['PYTHONPATH','PYTHONOPTIMIZE','PYTHONPYCACHEPREFIX','PYTHONIOENCODING']:env.pop(name,None)
try:
    for case in proof['cases']:
        source=ROOT/case['path'];module=ast.parse(source.read_bytes())
        constants=[n.value for n in ast.walk(module) if isinstance(n,ast.Constant) and isinstance(n.value,str)]
        assert case['literal'] in constants
        for role,command in [('cpython3147',[str(CP),'-I',str(source)]),('direct-native-parser',[str(HELPER),str(source)]),('xlang3',[str(RELEASE/'xlang3.exe'),str(source)])]:
            key=case['name']+'-'+role
            stdout=DATA/('sqlalchemy-triple-closing-comment-20261008-'+key+'.stdout.log')
            stderr=DATA/('sqlalchemy-triple-closing-comment-20261008-'+key+'.stderr.log')
            row=dict(case=case['name'],runtime=role,command=command)
            record['phases'].append(row);save()
            with stdout.open('xb') as out,stderr.open('xb') as err:
                child=subprocess.run(command,cwd=ROOT,env=env,stdin=subprocess.DEVNULL,stdout=out,stderr=err,
                    timeout=30,creationflags=subprocess.CREATE_NO_WINDOW)
            row.update(exit_code=child.returncode,stdout_log=stdout.name,stdout_sha256=sha(stdout),stderr_log=stderr.name,stderr_sha256=sha(stderr))
            expected=0 if role=='cpython3147' or not case['comment_present'] else 1
            assert child.returncode==expected,(key,child.returncode,stdout.read_bytes(),stderr.read_bytes())
            if role=='direct-native-parser':
                result=json.loads(stdout.read_bytes());row['result']=result
                assert Path(result['loaded_runtime_dll']).resolve()==(RELEASE/'xlang3_runtime.dll').resolve()
                assert bool(result['parser_errors'])==case['comment_present']
                assert stderr.read_bytes()==b''
            elif expected==0:
                assert stdout.read_bytes().replace(b'\r\n',b'\n')==case['expected_stdout'].encode()
                assert stderr.read_bytes()==b''
    record.update(status='terminal_closing_comment_failure_reproduced',failure_reproduced=True)
except BaseException as error:record.update(status='terminal_failed_diagnostic',error=repr(error))
finally:
    record['hashes_after']={p:sha(p) for p in before};record['hashes_unchanged']=record['hashes_after']==before
    if not record['hashes_unchanged']:record['status']='terminal_invalid_hash_drift'
    record['terminal']=True;save()
print(record['status'])
for row in record['phases']:print(row['case'],row['runtime'],row.get('exit_code'))
raise SystemExit(0 if record['failure_reproduced'] and record['hashes_unchanged'] else 1)
