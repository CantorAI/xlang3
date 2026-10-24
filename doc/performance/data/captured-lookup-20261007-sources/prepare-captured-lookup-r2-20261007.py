"""Strengthen fixture coverage; preserve failed first-validation evidence."""
from pathlib import Path

root = Path.cwd()
fixture = root / 'tests/fixtures/core/native_captured_lookup.py'
text = fixture.read_text(encoding='utf-8')
assert "key_a =" not in text
text = text.replace("token = object()", "token = object()\nkey_a = (b'a', b'right')\nkey_b = (b'b', b'right')")
text = text.replace("'a'", 'key_a').replace("'b'", 'key_b')
# Replace only string keys, keeping the bytes definitions literal.
text = text.replace('(bkey_a,', "(b'a',").replace('(bkey_b,', "(b'b',")
compile(text, str(fixture), 'exec')
fixture.write_text(text, encoding='utf-8')
scratch = root / 'scratch/performance'
code = (scratch / 'validate-captured-lookup-20261007.py').read_text(encoding='utf-8')
code = code.replace("prefix = 'captured-lookup-validation-20261007'", "prefix = 'captured-lookup-validation-r2-20261007'")
code = code.replace('release-captured-lookup-fixed-gate-20261007.json', 'release-captured-lookup-r2-fixed-gate-20261007.json')
code = code.replace('pyperformance-captured-lookup-bpe-fast-20261007.json', 'pyperformance-captured-lookup-r2-bpe-fast-20261007.json')
target = scratch / 'validate-captured-lookup-r2-20261007.py'
assert not target.exists()
compile(code, str(target), 'exec')
target.write_text(code, encoding='utf-8')
print('Prepared tuple-hit fixture and fresh r2 validation; original failure logs retained')
