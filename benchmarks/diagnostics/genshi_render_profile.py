"""Profile Genshi's timed body with the official template and input.

Construction and imports precede profiling, as in bm_genshi. Profile times and
native sampling are diagnostic evidence only, never pyperformance scores.
"""
import argparse
import hashlib
import json
import runpy
import time
from pathlib import Path


parser = argparse.ArgumentParser()
parser.add_argument('benchmark_script')
parser.add_argument('--variant', choices=('xml', 'text'), default='xml')
parser.add_argument('--repeat', type=int, default=1)
parser.add_argument('--profile', action='store_true')
parser.add_argument('--start-marker')
args = parser.parse_args()
namespace = runpy.run_path(args.benchmark_script, run_name='genshi_diagnostic_target')
template_type, template_source = namespace['BENCHMARKS'][args.variant]
template = template_type(template_source)
table = [dict(a=1, b=2, c=3, d=4, e=5, f=6, g=7, h=8, i=9, j=10)
         for _ in range(1000)]
profiler = None
if args.profile:
    import _lsprof
    profiler = _lsprof.Profiler()
if args.start_marker:
    Path(args.start_marker).write_text('render body ready', encoding='utf-8')
if profiler:
    profiler.enable()
started = time.perf_counter()
for _ in range(args.repeat):
    rendered = template.generate(table=table).render()
elapsed = time.perf_counter() - started
if profiler:
    profiler.disable()
rows = []
if profiler:
    grouped = {}
    for entry in profiler.getstats():
        code = entry.code
        name = code if isinstance(code, str) else '%s:%s:%s' % (
            code.co_filename.replace('\\', '/'), code.co_firstlineno, code.co_name)
        # XLang3 currently exposes fresh frame-code objects for repeated profile
        # events. Group by source location for useful call counts and bounded
        # output; retain entry_count so this profiler limitation stays visible.
        row = grouped.setdefault(name, {'function': name, 'calls': 0,
                                        'recursive_calls': 0, 'entry_count': 0,
                                        'inclusive_seconds': 0.0, 'self_seconds': 0.0})
        row['calls'] += entry.callcount
        row['recursive_calls'] += entry.reccallcount
        row['entry_count'] += 1
        row['inclusive_seconds'] += entry.totaltime
        row['self_seconds'] += entry.inlinetime
    rows = list(grouped.values())
    rows.sort(key=lambda row: row['self_seconds'], reverse=True)
record = {'variant': args.variant, 'repeat': args.repeat,
          'diagnostic_elapsed_seconds': elapsed,
          'characters': len(rendered), 'rows': rendered.count('<tr>'),
          'cells': rendered.count('<td>'),
          'sha256_utf8': hashlib.sha256(rendered.encode('utf-8')).hexdigest(),
          'profile': rows}
print(json.dumps(record))
expected = {'xml': (112017, '61096eb9fee3ea72a4615a57e653bca545efacbad8aa8d522bb954d66d9421bc'),
            'text': (112018, 'c2e6154871cc315eadf1536e6b5f4679e3eee7aa6ed6ac3b9ea2e927ef2de810')}
assert (record['characters'], record['sha256_utf8']) == expected[args.variant]
assert (record['rows'], record['cells']) == (1000, 10000)
