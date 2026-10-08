"""Apply the prepared native builtin change after preserving the accepted Release."""
from pathlib import Path
import subprocess

root = Path.cwd()
source = root / 'src/builtins/functional_builtins.cpp'
before = root / 'scratch/performance/minmax-streaming-20261007-source-before'
assert source.read_bytes() == (before / source.name).read_bytes()
assert source.read_bytes().replace(b'\r\n', b'\n') == subprocess.check_output(['git', 'show', 'HEAD:src/builtins/functional_builtins.cpp'])
text = source.read_text()
start = text.index('bool minmax_common(\n')
end = text.index('\nbool builtin_min(', start)
assert 'std::vector<Value> values;' in text[start:end]
assert 'std::vector<Value> keys;' in text[start:end]
replacement = (root / 'scratch/performance/minmax-streaming-function-next-20261007.cpp').read_text()
replacement = replacement[replacement.index('bool minmax_common(\n'):].rstrip() + '\n'
source.write_bytes((text[:start] + replacement + text[end:]).encode('utf-8'))
fixture = root / 'tests/fixtures/core/minmax_streaming.py'
expected = root / 'tests/fixtures/expected/minmax_streaming.out'
assert not fixture.exists() and not expected.exists()
body = (root / 'scratch/performance/minmax-streaming-fixture-next-20261007.py').read_text()
body = body.replace('Prepared fixture; run only after the current official benchmark is terminal.',
                    'Native min/max streaming, error propagation and owner lifetime regression.')
fixture.write_bytes(body.encode('utf-8'))
expected.write_bytes((before / 'minmax_streaming.out').read_bytes())
runner = root / 'tests/run_fixtures.py'
assert runner.read_bytes() == (before / runner.name).read_bytes()
text = runner.read_text()
anchor = 'CORE_CASES = """\n'
assert text.count(anchor) == 1
runner.write_bytes(text.replace(anchor, anchor + 'minmax_streaming\n').encode('utf-8'))
print('Applied native min/max streaming with a six-part semantic fixture')
