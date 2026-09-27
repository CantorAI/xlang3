class Custom:
    def __repr__(self):
        return 'CUSTOM'


value = Custom()
print('dict', str({'a': value}))
print('list', str([value]))
print('tuple', str((value,)))
print('set', str({value}))
