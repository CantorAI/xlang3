"""Root-only, untimed acceptance of unchanged SQLAlchemy syntax by CPython."""
import ast
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
CP = Path('C:/Python/Python314/python.exe')
SOURCE = ROOT / 'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages/sqlalchemy/sql/selectable.py'
EXPECTED = 'eac5df2ea20a1acbbe0d866bb09864f9e310e3718e676cd395274e17c60547f2'
OUT = ROOT / 'doc/performance/data/sqlalchemy-direct-parser-cpython3147-20261008.json'
digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()

assert Path(sys.executable).resolve() == CP.resolve()
assert sys.implementation.name == 'cpython' and sys.version_info[:3] == (3, 14, 7)
assert sys.flags.optimize == 0 and sys.flags.isolated == 1
assert not OUT.exists()
before = digest(SOURCE)
assert before == EXPECTED
raw = SOURCE.read_bytes()
record = dict(status='preflight', terminal=False, diagnostic_only=True, scored=False,
    runtime=sys.version, executable=str(CP), cpython_executable_sha256=digest(CP),
    controller_sha256=digest(Path(__file__)), source=str(SOURCE),
    source_sha256_before=before, source_byte_count=len(raw),
    module_imported=False, source_modified=False,
    started_utc=datetime.now(timezone.utc).isoformat())
try:
    module = ast.parse(raw, filename=str(SOURCE))
    ast.parse('def equal(other, self):\n    return other.of is self.of\n')
    record.update(status='cpython3147_unchanged_source_and_of_attribute_parse_passed',
        module_statement_count=len(module.body), of_attribute_control_passed=True)
except BaseException as error:
    record.update(status='failed', error=repr(error))
finally:
    record.update(source_sha256_after=digest(SOURCE), terminal=True,
        completed_utc=datetime.now(timezone.utc).isoformat())
    assert record['source_sha256_after'] == before
    OUT.write_bytes((json.dumps(record, indent=2) + '\n').encode())
print(record['status'])
raise SystemExit(0 if record['status'].endswith('parse_passed') else 1)
