args = [1, 2]
print('global', [args + 1 for args in args])


def local_case():
    args = [3, 4]
    return [args + 1 for args in args]


print('local', local_case())
print('nested', [[item for item in item] for item in [[5, 6], [7]]])
print('set', {item + 1 for item in args})
print('dict', {item: item + 1 for item in args})
