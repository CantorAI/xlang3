"""Record the actual completed root build, then check the reproduced syntax."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2];DATA=ROOT/'doc/performance/data'
CP=Path('C:/Python/Python314/python.exe');RELEASE=ROOT/'build-repro/main-verify-20261006/Release'
CONTROL=ROOT/'build-repro/controls/triple-string-closing-comment-parent-20261008'
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
tree=lambda d:{p.relative_to(d).as_posix():sha(p) for p in sorted(d.rglob('*')) if p.is_file()}
assert Path(sys.executable).resolve()==CP.resolve() and sys.version_info[:3]==(3,14,7)
assert sys.flags.optimize==0 and sys.flags.isolated==1
applied_path=DATA/'triple-string-closing-comment-applied-source-20261008.json'
applied=json.loads(applied_path.read_bytes());sources=applied['source_sha256']
parent=json.loads((CONTROL/'preserved-release-provenance.json').read_bytes())
assert applied['source_count']==len(sources)==124
assert all(sha(ROOT/p)==h for p,h in sources.items())
assert all(sha(ROOT/p)==h for p,h in applied['unowned_tracked_dirty_sha256'].items())
baseline=tree(ROOT/'build-repro/Release');assert baseline==parent['fixed_baseline_sha256']
release=tree(RELEASE);assert len(release)==178
build_path=DATA/'triple-string-closing-comment-build-terminal-20261008.json'
assert not build_path.exists()
log=DATA/'triple-string-closing-comment-build-20261008.log'
assert 'Linking CXX shared library Release\\xlang3_runtime.dll' in log.read_text()
build=dict(status='build_passed',terminal=True,actual_root_tool_exit_code=0,root_session_id=29370,
    log=str(log.relative_to(ROOT)),log_sha256=sha(log),configuration='Release',build_directory=str(RELEASE.parent),
    source_inventory_sha256=sha(applied_path),source_count=124,source_sha256=sources,
    binaries_sha256=release,candidate_binary_sha256=dict(exe=sha(RELEASE/'xlang3.exe'),dll=sha(RELEASE/'xlang3_runtime.dll')),
    baseline_sha256=baseline,performance_validation_pending=True)
build_path.write_bytes((json.dumps(build,indent=2)+'\n').encode())
proof_path=ROOT/'scratch/performance/sqlalchemy-triple-closing-comment-r2-provenance-proposed-20261008.json'
assert sha(proof_path)=='44378c66e1c140cbfde0ddbdb0182cfc0000f765ff5a2f0c19aaddeec1656bfb'
proof=json.loads(proof_path.read_bytes())
out=DATA/'triple-string-closing-comment-focused-20261008.json';assert not out.exists()
record=dict(status='preflight',terminal=False,source_count=124,source_sha256=sources,binaries_sha256=release,
    candidate_binary_sha256=build['candidate_binary_sha256'],source_inventory_sha256=sha(applied_path),
    build_receipt_sha256=sha(build_path),controller_sha256=sha(Path(__file__)),phases=[],full_correctness_pending=True,
    performance_validation_pending=True,original_reproduction_sha256='fcadeb7d1417867b4318158c2ef3140c7ea56b2f9c8af4875a2c4e596bf4673a')
def save():out.write_bytes((json.dumps(record,indent=2)+'\n').encode())
env=os.environ.copy();env['PATH']=str(RELEASE)+os.pathsep+env.get('PATH','')
env['XLANG3_PYTHON_LIB']=str(CP.parent/'Lib')
for key in ['PYTHONPATH','PYTHONOPTIMIZE','PYTHONIOENCODING','PYTHONPYCACHEPREFIX']:env.pop(key,None)
try:
    helper=ROOT/'scratch/performance/sqlalchemy-direct-native-parser-20261008/direct-parser.exe'
    commands=[('cpp-parser',[str(RELEASE/'xlang3_parser_tests.exe')],None)]
    for case in proof['cases']:
        source=ROOT/case['path'];assert sha(source)==case['sha256']
        commands += [(case['name']+'-xlang3',[str(RELEASE/'xlang3.exe'),str(source)],case['expected_stdout']),
            (case['name']+'-direct-parser',[str(helper),str(source)],None)]
    source=ROOT/'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages/sqlalchemy/sql/selectable.py'
    assert sha(source)=='eac5df2ea20a1acbbe0d866bb09864f9e310e3718e676cd395274e17c60547f2'
    commands += [('full-selectable-direct-parser',[str(helper),str(source)],None)]
    for name,command,expected in commands:
        stdout=DATA/f'triple-string-closing-comment-focused-20261008-{name}.stdout.log'
        stderr=DATA/f'triple-string-closing-comment-focused-20261008-{name}.stderr.log'
        row=dict(name=name,command=command,passed=False);record['phases'].append(row);save()
        with stdout.open('xb') as o,stderr.open('xb') as e:
            child=subprocess.run(command,cwd=ROOT,env=env,stdin=subprocess.DEVNULL,stdout=o,stderr=e,
                timeout=60,creationflags=subprocess.CREATE_NO_WINDOW)
        row.update(exit_code=child.returncode,stdout_log=stdout.name,stdout_sha256=sha(stdout),stderr_log=stderr.name,stderr_sha256=sha(stderr))
        assert child.returncode==0 and stderr.read_bytes()==b'',(name,child.returncode,stdout.read_bytes(),stderr.read_bytes())
        if expected is not None:assert stdout.read_bytes().replace(b'\r\n',b'\n')==expected.encode()
        if name.endswith('direct-parser'):
            result=json.loads(stdout.read_bytes());row['result']=result
            assert not result['parser_errors'] and not result['lexer_errors']
            assert Path(result['loaded_runtime_dll']).resolve()==(RELEASE/'xlang3_runtime.dll').resolve()
        row['passed']=True;save()
    record['status']='targeted_correctness_passed'
except BaseException as error:record.update(status='targeted_correctness_failed',error=repr(error))
finally:
    record['hashes_unchanged']=all(sha(ROOT/p)==h for p,h in sources.items()) and tree(RELEASE)==release and tree(ROOT/'build-repro/Release')==baseline
    record['terminal']=True;save()
print(record['status'],'phases',len(record['phases']),'hashstable',record['hashes_unchanged'])
raise SystemExit(0 if record['status']=='targeted_correctness_passed' and record['hashes_unchanged'] else 1)
