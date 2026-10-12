"""Prepared differential probe; run after the current official benchmark ends."""
import json


class Plain(dict):
    def __missing__(self, key):
        return ['type', key]


plain = Plain()
plain.__missing__ = lambda key: ['instance', key]


class Hooked(dict):
    def __getattribute__(self, name):
        if name == '__missing__':
            return lambda key: ['hook', key]
        return object.__getattribute__(self, name)

    def __missing__(self, key):
        return ['type', key]


class Descriptor:
    def __get__(self, instance, owner):
        return lambda key: [owner.__name__, key]


class Custom(dict):
    __missing__ = Descriptor()


class BindingFailure:
    def __get__(self, instance, owner):
        raise LookupError('descriptor binding failed')


class Broken(dict):
    __missing__ = BindingFailure()


observations = {}
for name, mapping in (('instance_override', plain), ('getattribute_hook', Hooked()),
                       ('custom_descriptor', Custom()), ('descriptor_error', Broken())):
    for kind, key in (('string', 'absent'), ('composite', (b'left', b'right'))):
        try:
            value = mapping[key]
            if isinstance(value, list):
                value = [value[0], kind]
            observations[name + '_' + kind] = {'value': value}
        except Exception as error:
            observations[name + '_' + kind] = {'error': type(error).__name__, 'message': str(error)}
print(json.dumps(observations, sort_keys=True))

# Explicit dict.__getitem__ must also use the type's __missing__. Storage-only
# operations must neither invoke __missing__ nor overridden __getitem__.
class StorageOnly(dict):
    def __missing__(self, key):
        raise AssertionError('storage operation called __missing__')

    def __getitem__(self, key):
        raise AssertionError('storage operation called __getitem__')


extra = {}
for name, operation in (
    ('explicit_getitem', lambda: dict.__getitem__(plain, 'absent')),
    ('contains', lambda: 'absent' in StorageOnly()),
    ('get', lambda: StorageOnly().get('absent', 7)),
    ('pop', lambda: StorageOnly().pop('absent', 7)),
    ('setdefault', lambda: StorageOnly().setdefault('absent', 7)),
):
    try:
        extra[name] = {'value': operation()}
    except Exception as error:
        extra[name] = {'error': type(error).__name__, 'message': str(error)}
print(json.dumps(extra, sort_keys=True))
