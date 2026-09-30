import gc


def no_extra_positional_args(*args):
    return args


empty = no_extra_positional_args()
assert empty == ()
assert not gc.is_tracked(empty)
assert not gc.is_tracked(tuple())
assert gc.is_tracked(([],))

print("empty tuples skip GC tracking; reference-bearing tuples stay tracked")
