"""Reference probes for live namespaces before optimizing generic eval.

Print observations rather than asserting compatibility, so the baseline also
records existing semantic gaps. Globals cannot be cached as stale snapshots.
"""
import json

result = {}
globals_mapping = {'x': 1}
def mutate():
    globals_mapping['x'] = 2
    return 0
globals_mapping['mutate'] = mutate
result['global_mutation_within_eval'] = eval(compile('mutate() + x', '<live>', 'eval'), globals_mapping, {})
result['builtins_inserted_into_original'] = '__builtins__' in globals_mapping
result['globals_identity'] = eval('globals()', globals_mapping, {}) is globals_mapping
callback = eval('lambda: x', globals_mapping, {})
globals_mapping['x'] = 3
result['nested_function_sees_later_global_mutation'] = callback()
result['explicit_locals_shadow_globals'] = eval('x', globals_mapping, {'x': 7})
result['custom_builtins'] = eval('len([1, 2])', {'__builtins__': {'len': lambda value: 42}}, {})
print(json.dumps(result))
