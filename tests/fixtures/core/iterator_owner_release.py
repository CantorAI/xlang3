import gc
import itertools
import tokenize
import weakref


class Owner:
    def __init__(self):
        self.read = False

    def __call__(self):
        return 1

    def __iter__(self):
        return self

    def __next__(self):
        return 1


class Reader:
    def __init__(self):
        self.read = False

    def __call__(self):
        if self.read:
            return ''
        self.read = True
        return 'x = (1, 2)\n'


class GetitemOwner:
    def __getitem__(self, index):
        if index == 0:
            return 1
        raise IndexError(index)


def leave_owner(case):
    owner = Reader() if case == 'tokenizer' else GetitemOwner() if case == 'getitem' else Owner()
    reference = weakref.ref(owner)
    if case == 'callable':
        iterator = iter(owner, 0)
        assert next(iterator) == 1
    elif case == 'chain':
        iterator = itertools.chain([owner])
        assert next(iterator) is owner
    elif case == 'chain-from':
        iterator = itertools.chain.from_iterable([[owner]])
        assert next(iterator) is owner
    elif case == 'tokenizer':
        iterator = tokenize.generate_tokens(owner)
        for token in iterator:
            break
    else:
        for value in owner:
            break
    return reference


for case in ('protocol', 'getitem', 'callable', 'chain', 'chain-from', 'tokenizer'):
    references = [leave_owner(case) for _ in range(100)]
    gc.collect()
    print(case, all(reference() is None for reference in references))


def retained_tokens():
    reader = Reader()
    reference = weakref.ref(reader)
    tokens = list(tokenize.generate_tokens(reader))
    return reference, tokens


reference, tokens = retained_tokens()
gc.collect()
print('yielded-values-survive', reference() is None, tokens[0].string == 'x', tokens[0].start == (1, 0))
