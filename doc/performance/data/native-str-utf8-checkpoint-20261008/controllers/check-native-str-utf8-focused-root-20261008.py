"""Fresh CPP and Python semantic checks for the built frozen UTF-8 trial."""
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path('D:/CantorAI/xlang3')
DATA=ROOT/'doc/performance/data'
CP=Path('C:/Python/Python314/python.exe')
RELEASE=ROOT/'build-repro/main-verify-20261006/Release'
BASELINE=ROOT/'build-repro/Release'
PARENT=ROOT/'build-repro/controls/native-str-utf8-encode-parent-20261008'
APP=DATA/'native-str-utf8-applied-source-20261008.json'
APP_SHA='96ba3820206e00029a26231b15accb79ff94e1ba33098d769e586d7a28b12f83'
CP_PRE=DATA/'native-str-utf8-cpython-semantics-20261008.json'
CP_PRE_SHA='f3c91eb53ca3f903838367c2bd111b72ba778ae36d507d88b4f4fa95d0f313e4'
PREFIX='native-str-utf8-focused-20261008'
NAMES=('native_str_utf8_encode','string_compat','unicode_repr_surrogate','pickle_module',
       'sys_monitoring_all_events','trace_hooks','nested_profile_setting')
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
read=lambda p:json.loads(Path(p).read_bytes())
tree=lambda root:{p.relative_to(root).as_posix():sha(p) for p in sorted(root.rglob('*')) if p.is_file()}

def main():
    assert Path(sys.executable).resolve()==CP.resolve() and sys.version_info[:3]==(3,14,7) and not sys.flags.optimize
    assert sha(APP)==APP_SHA and sha(CP_PRE)==CP_PRE_SHA and read(CP_PRE)['passed']
    app=read(APP)
    assert app['terminal'] and app['source_count']==119 and app['unowned_tracked_bytes_preserved']
    sources=app['source_sha256']; release=tree(RELEASE)
    assert len(release)==178 and tree(BASELINE)==app['fixed_baseline_sha256']
    parent_manifest=PARENT/'preserved-release-provenance.json'
    assert sha(parent_manifest)==app['parent_manifest_sha256']
    parent=read(parent_manifest)
    assert tree(PARENT/'Release')==parent['files_sha256']
    assert all(sha(ROOT/p)==value for p,value in sources.items())
    pins={ROOT/p:value for p,value in sources.items()}
    pins.update({APP:APP_SHA,CP_PRE:CP_PRE_SHA,CP:sha(CP),CP.with_name('python314.dll'):sha(CP.with_name('python314.dll'))})
    output=DATA/(PREFIX+'.json'); assert not output.exists()
    record=dict(status='running_targeted_correctness',terminal=False,scored=False,
                source_inventory_sha256=APP_SHA,source_sha256=sources,source_count=len(sources),
                binaries_sha256={RELEASE.relative_to(ROOT).as_posix()+'/'+p:value for p,value in release.items()},
                candidate_binary_sha256=dict(exe=release['xlang3.exe'],dll=release['xlang3_runtime.dll']),
                fixed_baseline_sha256=app['fixed_baseline_sha256'],cpython_preflight_sha256=CP_PRE_SHA,
                phases=[],started_utc=datetime.now(timezone.utc).isoformat())
    save=lambda:output.write_text(json.dumps(record,indent=2)+'\n',encoding='utf-8')
    env=dict(os.environ,XLANG3_PYTHON_LIB=str(CP.parent/'Lib'),PYTHONUNBUFFERED='1')
    for key in ('PYTHONPATH','PYTHONPYCACHEPREFIX','PYTHONIOENCODING','PYTHONOPTIMIZE'):env.pop(key,None)
    def stable():
        return all(sha(p)==value for p,value in pins.items()) and tree(RELEASE)==release and tree(BASELINE)==app['fixed_baseline_sha256']
    try:
        for name in ('cpp',)+NAMES:
            assert stable()
            row=dict(name=name,timeout=False,passed=False)
            if name=='cpp': command=[str(RELEASE/'xlang3_interpreter_tests.exe')]; expected_bytes=b''
            else:
                fixture=ROOT/'tests/fixtures/core'/f'{name}.py'
                expected=ROOT/'tests/fixtures/expected'/f'{name}.out'
                pins[fixture],pins[expected]=sha(fixture),sha(expected)
                command=[str(RELEASE/'xlang3.exe'),str(fixture)]
                expected_bytes=expected.read_bytes()
                row.update(source=fixture.relative_to(ROOT).as_posix(),source_sha256=sha(fixture),
                           expected=expected.relative_to(ROOT).as_posix(),expected_sha256=sha(expected))
            row['command']=command
            record['phases'].append(row);save()
            stdout,stderr=DATA/(PREFIX+'-'+name+'.stdout.log'),DATA/(PREFIX+'-'+name+'.stderr.log')
            with stdout.open('xb') as out,stderr.open('xb') as err:
                child=subprocess.Popen(command,cwd=ROOT,env=env,stdout=out,stderr=err,
                                       stdin=subprocess.DEVNULL,creationflags=subprocess.CREATE_NO_WINDOW)
                row['pid']=child.pid
                try:row['exit_code']=child.wait(timeout=120)
                except subprocess.TimeoutExpired:
                    row['timeout']=True
                    subprocess.run(['taskkill','/F','/T','/PID',str(child.pid)],capture_output=True,timeout=15)
                    row['exit_code']=child.wait(timeout=15)
            for stream,path in (('stdout',stdout),('stderr',stderr)):
                row[stream+'_log'],row[stream+'_sha256']=path.name,sha(path)
            row['passed']=not row['timeout'] and row['exit_code']==0 and stderr.read_bytes()==b'' and stdout.read_bytes().replace(b'\r\n',b'\n')==expected_bytes.replace(b'\r\n',b'\n')
            save()
            assert row['passed'], name
        assert stable()
        record.update(status='targeted_correctness_passed',hashes_unchanged=True)
    except BaseException as exc:
        record.update(status='targeted_correctness_failed',hashes_unchanged=stable(),error=repr(exc))
        raise
    finally:
        record.update(terminal=True,completed_utc=datetime.now(timezone.utc).isoformat());save()
        print(json.dumps({'status':record['status'],'phases':len(record['phases']),'receipt_sha256':sha(output)}))

if __name__=='__main__':main()
