# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0.
"""CP-first caller-state probe; no timers, counters or frame retention."""
import gc

CALLER_NAMESPACE_MARKER = 'constructor caller namespace'
observations = []
# Separate function globals make a retired init namespace distinguishable from
# the caller's namespace. All frame data are reduced to primitives in __del__.
init_namespace = {'__name__': 'constructor_init_namespace',
                  'observations': observations}
exec('''
import sys

class CompletionBox:
    def __new__(cls, value):
        return object.__new__(cls)

    def __init__(self, value):
        self.value = value

    def __del__(self):
        if self.value == 0:
            caller = sys._getframe().f_back
            observations.append((caller.f_code.co_name,
                                 caller.f_globals.get('CALLER_NAMESPACE_MARKER'),
                                 caller.f_locals.get('caller_local_marker')))
''', init_namespace)
CompletionBox = init_namespace['CompletionBox']


def construct_results(kind):
    caller_local_marker = 'constructor caller local'
    for value in (0, 1):
        # CP discards the first expression immediately. Current VM Pop is a
        # no-op; its ignored first output can be the sole previous owner when
        # this SAME Call destination is replaced on the second iteration.
        # Only that first instance probes; the final instance's later cleanup
        # does not depend on the caller's locals after frame retirement.
        kind(value)


construct_results(CompletionBox)
gc.collect()
assert observations == [('construct_results', 'constructor caller namespace',
                         'constructor caller local')], observations
print('PASS python-new completion finalizer sees caller frame globals locals')
