"""Check the actual rendered workload before trusting Genshi speed ratios.

This is a correctness probe, not a replacement benchmark. Templates and input
match pyperformance's bm_genshi; no timing or library algorithm is replaced.
Compare hashes between runtimes separately for XML and text serialization.
"""
import hashlib
import json
import sys

from genshi.template import MarkupTemplate, NewTextTemplate


BIGTABLE_XML = """\
<table xmlns:py="http://genshi.edgewall.org/">
<tr py:for="row in table">
<td py:for="c in row.values()" py:content="c"/>
</tr>
</table>
"""
BIGTABLE_TEXT = """\
<table>
{% for row in table %}<tr>
{% for c in row.values() %}<td>$c</td>{% end %}
</tr>{% end %}
</table>
"""


def main():
    table = [dict(a=1, b=2, c=3, d=4, e=5, f=6, g=7, h=8, i=9, j=10)
             for _ in range(1000)]
    rows = []
    for name, template_type, template_source in (
            ('xml', MarkupTemplate, BIGTABLE_XML),
            ('text', NewTextTemplate, BIGTABLE_TEXT)):
        rendered = template_type(template_source).generate(table=table).render()
        row = {'variant': name, 'characters': len(rendered),
               'sha256_utf8': hashlib.sha256(rendered.encode('utf-8')).hexdigest(),
               'table_rows': rendered.count('<tr>'),
               'table_cells': rendered.count('<td>'),
               'first_value_cells': rendered.count('<td>1</td>'),
               'last_value_cells': rendered.count('<td>10</td>'),
               'start': rendered[:200], 'end': rendered[-200:]}
        rows.append(row)
    evidence = {'rows': rows,
                'loaded_genshi_modules': {
                    name: getattr(module, '__file__', None)
                    for name, module in sys.modules.items()
                    if name == 'genshi' or name.startswith('genshi.')},
                'purpose': 'correctness only; identical 1000-row, 10000-cell workload'}
    print(json.dumps(evidence))
    for row in rows:
        assert row['table_rows'] == 1000, row
        assert row['table_cells'] == 10000, row
        assert row['first_value_cells'] == row['last_value_cells'] == 1000, row


if __name__ == '__main__':
    main()
