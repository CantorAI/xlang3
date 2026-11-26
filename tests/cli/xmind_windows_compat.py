"""Windows paths and child bytecode policy used by xMind. Run with xlang3 -B."""
import argparse
import json
import os
from pathlib import Path
import shutil
import site
import subprocess
import sys
import tempfile

if os.name != 'nt':
    raise SystemExit('Windows-only compatibility fixture')

parser = argparse.ArgumentParser()
parser.add_argument('--executable', default=sys.executable)
options = parser.parse_args()

parent = Path(__file__).resolve().parents[2] / 'build' / 'test-temp'
parent.mkdir(parents=True, exist_ok=True)
root = Path(tempfile.mkdtemp(prefix='xmind-windows-', dir=str(parent)))
work = root / 'workspace 雪'
library = root / 'library 雪'
work.mkdir()
library.mkdir()
shutil.copyfile(site.__file__, library / 'site.py')
package = library / 'probe_package'
package.mkdir()
(package / '__init__.py').write_text('', encoding='utf-8')
(package / 'child.py').write_text('VALUE = 42\n', encoding='utf-8')
env = dict(os.environ)
env['PYTHONDONTWRITEBYTECODE'] = '1'

def run(label, code, cwd, environment, flags=()):
    result = subprocess.run([options.executable, *flags, '-c', code], cwd=str(cwd),
                            env=environment, capture_output=True, text=True)
    assert result.returncode == 0, (label, result.returncode, result.stdout, result.stderr)
    print(label + ': ' + result.stdout.strip())

run('unicode-cwd', 'import os; print(os.getcwd())', work, env)
unicode_env = dict(env)
unicode_env['XLANG3_PYTHON_LIB'] = str(library)
run('unicode-library', 'import os,site; assert os.path.samefile(site.__file__, '
    + repr(str(library / 'site.py')) + '); print(site.__file__)', root, unicode_env)
run('inherited-bytecode-policy', 'import sys; assert sys.dont_write_bytecode; print("enabled")', root, env)
run('ignore-environment', 'import sys; assert not sys.dont_write_bytecode; print("ignored")', root, env, ('-E', '-S'))
run('unicode-package-child', 'import probe_package.child as child; assert child.VALUE == 42; print(child.__file__)', root, unicode_env)
cache_env = dict(unicode_env)
cache_env.pop('PYTHONDONTWRITEBYTECODE', None)
run('unicode-package-cache', 'import os,sys,probe_package.child as child; assert not sys.dont_write_bytecode; assert os.path.isfile(child.__cached__), child.__cached__; print(child.__cached__)', root, cache_env)
print(json.dumps({'passed': 6, 'retained_fixture': str(root)}))
