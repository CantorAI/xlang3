import json
from pathlib import Path
import statistics

path = Path('doc/performance/data/dict-native-dispatch-followup-20261007.json')
data = json.loads(path.read_text(encoding='utf-8'))
assert data['status'] == 'terminal'
for probe in ('inherited_subscripts', 'callback_boundary'):
    print(probe)
    for observation in data['observations']:
        if observation['probe'] != probe:
            continue
        for row in observation['result']['rows']:
            if row.get('key_count', 1024) != 1024:
                continue
            print(observation['runtime'], row.get('mapping', ''), row['path'],
                  'median_seconds', statistics.median(row['samples_seconds']))
order = [item for item in data['observations'] if item['probe'] == 'minmax_order']
left, right = [item['result'] for item in order]
for key in left:
    if left[key] != right[key]:
        print('Semantic difference', key)
        print('CPython:', left[key])
        print('XLang3:', right[key])
