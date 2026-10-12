# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# http://www.apache.org/licenses/LICENSE-2.0
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Own native coverage.tracer contracts, without the Python Coverage package."""
import gc
import os
import sys
import weakref
from coverage.tracer import CTracer, CFileDisposition

TRACER_FIELDS = ('data', 'trace_arcs', 'should_trace', 'should_trace_cache', 'warn',
                 'concur_id_func', 'file_tracers', 'check_include',
                 'should_start_context', 'switch_context', 'disable_plugin')
DISPOSITION_FIELDS = ('original_filename', 'canonical_filename', 'source_filename',
                      'trace', 'reason', 'file_tracer', 'has_dynamic_filename')
THIS = os.path.normcase(os.path.abspath(__file__))
assert sys.gettrace() is None and sys.getprofile() is None


def disposition(filename, frame):
    value = CFileDisposition()
    value.original_filename = filename
    value.canonical_filename = filename
    value.trace = os.path.normcase(os.path.abspath(filename)) == THIS
    value.source_filename = filename if value.trace else None
    value.reason = ''
    value.file_tracer = None
    value.has_dynamic_filename = False
    return value


def make_tracer(arcs=False):
    tracer = CTracer()
    tracer.data = {}
    tracer.trace_arcs = arcs
    tracer.should_trace = disposition
    tracer.should_trace_cache = {}
    tracer.warn = lambda *args, **kwargs: None
    tracer.concur_id_func = None
    tracer.file_tracers = {}
    tracer.check_include = lambda filename, frame: True
    tracer.should_start_context = None
    tracer.switch_context = None
    tracer.disable_plugin = None
    return tracer


def test_members():
    for target, fields in ((CTracer(), TRACER_FIELDS), (CFileDisposition(), DISPOSITION_FIELDS)):
        for name in fields:
            assert getattr(target, name) is None
            marker = object()
            setattr(target, name, marker)
            assert getattr(target, name) is marker
            delattr(target, name)
            assert getattr(target, name) is None
    assert not hasattr(CTracer(), 'threading')


def branch_marker(flag):
    if flag:
        return 5
    return 7


def test_packed_arcs():
    tracer = make_tracer(True)
    tracer.start()
    try:
        assert branch_marker(True) == 5 and branch_marker(False) == 7
    finally:
        tracer.stop()
        sys.settrace(None)
    arcs = set()
    first_line = branch_marker.__code__.co_firstlineno
    own_lines = set(range(first_line, first_line + 4))
    for values in tracer.data.values():
        for packed in values:
            assert isinstance(packed, int) and not isinstance(packed, bool)
            first = packed & 0xFFFFF
            second = (packed >> 20) & 0xFFFFF
            if packed & (1 << 40):
                first = -first
            if packed & (1 << 41):
                second = -second
            if abs(first) in own_lines or abs(second) in own_lines:
                arcs.add((first, second))
    assert arcs == {(-first_line, first_line + 1), (first_line + 1, first_line + 2),
                    (first_line + 2, -first_line), (first_line + 1, first_line + 3),
                    (first_line + 3, -first_line)}, arcs


def test_activity_stop():
    tracer = make_tracer()
    assert tracer.activity() is False
    assert tracer.start() is tracer and sys.gettrace() is tracer
    try:
        assert branch_marker(True) == 5
        assert tracer.activity() is True
        tracer.reset_activity(); assert tracer.activity() is False
        assert tracer.activity() is True
        def deferred_stop():
            tracer.stop(); immediate = sys.gettrace()
            after_event = sys.gettrace()
            return immediate is tracer, after_event is None
        assert deferred_stop() == (True, True)
    finally:
        tracer.stop()
        sys.settrace(None)


class MarkerError(Exception):
    pass


def test_callback_exception():
    marker = MarkerError('native-core-policy-marker')
    def broken_policy(filename, frame):
        raise marker
    tracer = make_tracer()
    tracer.should_trace = broken_policy
    tracer.start()
    caught = None
    def trigger():
        return 11
    try:
        try:
            trigger()
        except MarkerError as error:
            caught = error
        assert caught is marker
        assert sys.gettrace() is tracer
    finally:
        tracer.stop()
        sys.settrace(None)


class CycleNode:
    pass


def rooted_members():
    first, second = CycleNode(), CycleNode()
    tracer, value = make_tracer(), CFileDisposition()
    first.native = tracer
    second.native = value
    tracer.warn = first
    value.reason = second
    return weakref.ref(first), weakref.ref(second), tracer, value


def test_rooted_cycles():
    first, second, tracer, value = rooted_members()
    gc.collect()
    assert first().native is tracer and second().native is value
    # Rooted member edges survive collection. Explicitly break both cycles;
    # no assertion about CP's untracked base native cycles is implied.
    tracer.warn = None
    value.reason = None
    del tracer, value
    gc.collect()
    assert first() is None and second() is None


def test_retained_trace_frame_locals():
    retained = []
    def policy(filename, frame):
        if frame.f_code.co_name == 'lazy_target':
            retained.append(frame)
        return disposition(filename, frame)
    tracer = make_tracer()
    tracer.should_trace = policy
    tracer.start()
    try:
        def lazy_target():
            removed = 1
            value = 3
            del removed
            value = 29
            return value
        assert lazy_target() == 29
    finally:
        tracer.stop()
        sys.settrace(None)
    assert len(retained) == 1
    locals_after_return = retained[0].f_locals
    assert locals_after_return['value'] == 29
    assert 'removed' not in locals_after_return


for case in (test_members, test_packed_arcs, test_activity_stop,
             test_callback_exception, test_rooted_cycles,
             test_retained_trace_frame_locals):
    try:
        case()
    finally:
        sys.settrace(None)
        sys.setprofile(None)
    print('PASS ' + case.__name__)
