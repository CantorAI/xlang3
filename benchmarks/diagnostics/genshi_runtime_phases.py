"""Bound Genshi costs before optimizing shared runtime paths.

These are diagnostics, not official suite results. Genshi stays unmodified.
The stream probe excludes serialization; expression probes use Genshi's
existing evaluator and lookup functions, plus the same compiled code directly.
"""
import argparse
import json
import runpy
import time
from genshi.template.base import Context
from genshi.template.eval import Expression

parser = argparse.ArgumentParser()
parser.add_argument('benchmark_script')
args = parser.parse_args()
benchmark = runpy.run_path(args.benchmark_script, run_name='genshi_phase_target')
table = [dict(a=1, b=2, c=3, d=4, e=5, f=6, g=7, h=8, i=9, j=10)
         for _ in range(1000)]
rows = []
for variant in ('text', 'xml'):
    template_type, source = benchmark['BENCHMARKS'][variant]
    template = template_type(source)
    stream = list(template.generate(table=table))
    expected_count = len(stream)
    started = time.perf_counter()
    for _ in range(3):
        stream = list(template.generate(table=table))
    elapsed = (time.perf_counter() - started) / 3
    assert len(stream) == expected_count
    rows.append({'phase': variant + '_generate_events', 'elapsed_per_pass': elapsed,
                 'events': len(stream)})
expression = Expression('c')
context = Context(c=1)
globals_mapping = expression._globals(context)
locals_mapping = {'__data__': context}
code = expression.code
lookup = globals_mapping['_lookup_name']
for name in ('context_get', 'lookup_name', 'compiled_eval', 'expression_evaluate'):
    total = 0
    started = time.perf_counter()
    if name == 'context_get':
        for _ in range(10000):
            total += context.get('c')
    elif name == 'lookup_name':
        for _ in range(10000):
            total += lookup(context, 'c')
    elif name == 'compiled_eval':
        for _ in range(10000):
            total += eval(code, globals_mapping, locals_mapping)
    else:
        for _ in range(10000):
            total += expression.evaluate(context)
    elapsed = time.perf_counter() - started
    assert total == 10000
    rows.append({'phase': name, 'elapsed_per_pass': elapsed, 'calls': 10000,
                 'checksum': total})
print(json.dumps({'diagnostic_only': True, 'rows': rows}))
