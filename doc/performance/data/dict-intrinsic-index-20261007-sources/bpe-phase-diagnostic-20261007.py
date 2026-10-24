"""Attribute the full official algorithm to phases; instrumented, unscored."""
import hashlib
import json
from pathlib import Path
import runpy
import time

script = Path(r'C:\Python\Python314\Lib\site-packages\pyperformance\data-files\benchmarks\bm_bpe_tokeniser\run_benchmark.py')
source = script.read_text(encoding='utf-8')
assert hashlib.sha256(script.read_bytes()).hexdigest() == 'c7255345499e118181785370b0996e8cc1056491b9678b3fbeb02421e3f9b2df'
namespace = runpy.run_path(str(script), run_name='bpe_phase_diagnostic')
trainer_source = source[source.index('def bpe_train('):source.index('\ndef train(')]
changes = (
    ('    ranks = {}', '    phase_start = time.perf_counter()\n    ranks = {}'),
    ('    while len(ranks) < vocab_size:',
     '    phases["setup"] += time.perf_counter() - phase_start\n    while len(ranks) < vocab_size:'),
    ('        stats = collections.Counter()',
     '        phase_start = time.perf_counter()\n        stats = collections.Counter()'),
    ('        most_common_pair = max(stats, key=lambda x: stats[x])',
     '        phases["counting"] += time.perf_counter() - phase_start\n'
     '        phase_start = time.perf_counter()\n'
     '        most_common_pair = max(stats, key=lambda x: stats[x])'),
    ('        new_words = []',
     '        phases["selection"] += time.perf_counter() - phase_start\n'
     '        phase_start = time.perf_counter()\n        new_words = []'),
    ('        words = new_words',
     '        words = new_words\n        phases["merging"] += time.perf_counter() - phase_start'),
)
for before, after in changes:
    assert trainer_source.count(before) == 1, before
    trainer_source = trainer_source.replace(before, after)
phases = dict.fromkeys(('setup', 'counting', 'selection', 'merging', 'encoding'), 0.0)
globals_dict = namespace['bpe_train'].__globals__
globals_dict.update(time=time, phases=phases)
exec(compile('from __future__ import annotations\n' + trainer_source,
             '<instrumented official BPE trainer>', 'exec'), globals_dict)
pattern = r"'s|'t|'re|'ve|'m|'ll|'d| ?[a-zA-Z]+| ?\d+| ?[^\sa-zA-Z\d]+|\s+(?!\S)|\s+"
data = (script.parent / 'data/frankenstein_intro.txt').read_text(encoding='utf-8')
ranks = globals_dict['bpe_train'](data=data, vocab_size=1024, pat_str=pattern)
start = time.perf_counter()
enc = namespace['SimpleBytePairEncoding'](pat_str=pattern, mergeable_ranks=ranks)
tokens = enc.encode('hello world')
assert enc.decode(tokens) == 'hello world'
encoded = enc.encode(data)
phases['encoding'] = time.perf_counter() - start
assert len(ranks) == 1024
serial_ranks = [(key.hex(), value) for key, value in sorted(ranks.items(), key=lambda item: item[1])]
print(json.dumps({'purpose': 'instrumented full algorithm phase attribution only, never scored',
                  'phases_seconds': phases, 'vocab_size': len(ranks),
                  'ranks_sha256': hashlib.sha256(json.dumps(serial_ranks).encode()).hexdigest(),
                  'encoded_token_count': len(encoded),
                  'encoded_tokens_sha256': hashlib.sha256(json.dumps(encoded).encode()).hexdigest()}))
