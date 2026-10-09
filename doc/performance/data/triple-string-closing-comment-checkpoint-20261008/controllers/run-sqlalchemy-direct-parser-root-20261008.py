"""Root-only build/run of an untimed parser diagnostic, without engine edits."""
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
DATA = ROOT / 'doc/performance/data'
CP = Path('C:/Python/Python314/python.exe')
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
SCRATCH = ROOT / 'scratch/performance'
OUT = DATA / 'sqlalchemy-direct-native-parser-root-20261008.json'
assert Path(sys.executable).resolve() == CP.resolve() and sys.version_info[:3] == (3,14,7)
assert sys.flags.isolated == 1 and sys.flags.optimize == 0 and not OUT.exists()
sha = lambda p: hashlib.sha256(Path(p).read_bytes()).hexdigest()
paths = [Path(__file__), SCRATCH/'sqlalchemy-direct-native-parser-proposed-20261008.cpp',
    SCRATCH/'compile-sqlalchemy-direct-native-parser-proposed-20261008.cmd',
    SCRATCH/'sqlalchemy-of-attribute-valid-control-proposed-20261008.py',
    SCRATCH/'sqlalchemy-typo-hint-error-context-control-proposed-20261008.py',
    ROOT/'src/internal/xlang3/parser.h', ROOT/'src/internal/xlang3/ast.h',
    ROOT/'src/parser/parser.cpp', ROOT/'src/parser/lexer.cpp',
    RELEASE/'xlang3.exe', RELEASE/'xlang3_runtime.dll', RELEASE/'xlang3_runtime.lib',
    ROOT/'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages/sqlalchemy/sql/selectable.py',
    DATA/'sqlalchemy-direct-parser-cpython3147-20261008.json', CP]
before = {str(p):sha(p) for p in paths}
assert before[str(RELEASE/'xlang3_runtime.dll')] == '64640e503a103341dd44d220eba17ef802219b9df639bb1be5cbe5f2bf847564'
record = dict(status='preflight', terminal=False, diagnostic_only=True, scored=False,
    engine_rebuilt=False, library_modified=False, hashes_before=before, phases=[],
    source_identity_limit='Named files only; no complete transitive-header or clean-checkout claim',
    started_utc=datetime.now(timezone.utc).isoformat())
def save(): OUT.write_bytes((json.dumps(record,indent=2)+'\n').encode())
def run(name, command, env=None, timeout=60):
    stdout = DATA/f'sqlalchemy-direct-native-parser-20261008-{name}.stdout.log'
    stderr = DATA/f'sqlalchemy-direct-native-parser-20261008-{name}.stderr.log'
    row = dict(name=name,command=command,stdout_log=stdout.name,stderr_log=stderr.name)
    record['phases'].append(row); save()
    with stdout.open('xb') as out, stderr.open('xb') as err:
        child = subprocess.run(command,cwd=ROOT,env=env,stdin=subprocess.DEVNULL,
            stdout=out,stderr=err,timeout=timeout,creationflags=subprocess.CREATE_NO_WINDOW)
    row.update(exit_code=child.returncode,stdout_sha256=sha(stdout),stderr_sha256=sha(stderr))
    save()
    return row,stdout,stderr
try:
    row,_,_ = run('compile',[str(Path(os.environ['SystemRoot'])/'System32/cmd.exe'),'/d','/c',str(paths[2])],timeout=120)
    assert row['exit_code'] == 0
    helper = SCRATCH/'sqlalchemy-direct-native-parser-20261008/direct-parser.exe'
    record['helper_sha256'] = sha(helper)
    env = os.environ.copy(); env['PATH'] = str(RELEASE)+os.pathsep+env.get('PATH','')
    for name, source, expected_exit in [('valid-of',paths[3],0),('invalid-context',paths[4],1),('full-selectable',paths[12],1)]:
        row,stdout,stderr = run(name,[str(helper),str(source)],env=env)
        assert row['exit_code'] == expected_exit and stderr.read_bytes() == b''
        result = json.loads(stdout.read_bytes()); row['result'] = result
        assert Path(result['loaded_runtime_dll']).resolve() == (RELEASE/'xlang3_runtime.dll').resolve()
        assert result['input_bytes'] == source.stat().st_size
        assert not result['parse_source_wrapper_used'] and not result['imports_executed'] and not result['python_executed']
    record['status'] = 'terminal_direct_native_parser_diagnosis'
except BaseException as error:
    record.update(status='terminal_failed_diagnostic',error=repr(error))
finally:
    record['hashes_after'] = {str(p):sha(p) for p in paths}
    record['hashes_unchanged'] = before == record['hashes_after']
    if not record['hashes_unchanged']: record['status'] = 'terminal_invalid_hash_drift'
    record.update(terminal=True,completed_utc=datetime.now(timezone.utc).isoformat()); save()
print(record['status'])
for row in record['phases']:
    if 'result' in row: print(row['name'],json.dumps(row['result']['parser_errors']))
raise SystemExit(0 if record['status']=='terminal_direct_native_parser_diagnosis' else 1)
