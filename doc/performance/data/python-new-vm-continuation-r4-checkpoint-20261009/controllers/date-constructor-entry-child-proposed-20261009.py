# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0.
"""Artificial constructor-entry diagnostic; no benchmark score or workload share."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import time

CP = Path('C:/Python/Python314/python.exe')
X = Path('D:/CantorAI/xlang3/build-repro/main-verify-20261006/Release/xlang3.exe')
DATETIME = Path('C:/Python/Python314/Lib/datetime.py')
PYDATETIME = Path('C:/Python/Python314/Lib/_pydatetime.py')
DATETIME_SHA = '8262c677654011417ae08751ae9c38edc8ca802d21c65232452529248a17aae9'
PYDATETIME_SHA = '63249ee7bac11a6c1cee7fae0355f1432d17c60bd3d503346cf9eab2cb875ec9'
OPERATIONS = 10000
STATE = b'\x07\xbc\x05\x07'
FIELDS = (1980, 5, 7)
CASES = ('date_class', 'date_saved_new', 'plain_class', 'plain_saved_new')
sha = lambda path: hashlib.sha256(Path(path).read_bytes()).hexdigest()


class EntryOnly:
    __slots__ = ()

    def __new__(cls, state):
        return object.__new__(cls)


def metadata(fn):
    code = getattr(fn, '__code__', None)
    return dict(type_name=type(fn).__name__, has_python_code=code is not None,
                filename=None if code is None else code.co_filename,
                first_line=None if code is None else code.co_firstlineno)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--selection', choices=('cpython-python', 'xlang3-public'), required=True)
    parser.add_argument('--case-order', required=True)
    parser.add_argument('--exe-sha256', required=True)
    parser.add_argument('--dll-sha256', required=True)
    args = parser.parse_args()
    cpython = args.selection == 'cpython-python'
    executable = CP if cpython else X
    dll = CP.with_name('python314.dll') if cpython else X.with_name('xlang3_runtime.dll')
    assert sys.flags.optimize == 0 and sys.version_info[:3] == (3, 14, 7)
    assert sys.implementation.name == ('cpython' if cpython else 'xlang3')
    assert Path(sys.executable).resolve() == executable.resolve()
    assert sys.gettrace() is None and sys.getprofile() is None
    pins = {str(executable): args.exe_sha256, str(dll): args.dll_sha256,
            str(DATETIME): DATETIME_SHA, str(PYDATETIME): PYDATETIME_SHA}
    assert all(sha(p) == h for p, h in pins.items())
    order = args.case_order.split(',')
    assert len(order) == 4 and set(order) == set(CASES)
    import datetime
    import _pydatetime
    assert Path(datetime.__file__).resolve() == DATETIME.resolve()
    assert Path(_pydatetime.__file__).resolve() == PYDATETIME.resolve()
    if cpython:
        import _datetime
        assert datetime.date is _datetime.date and datetime.date is not _pydatetime.date
    else:
        try:
            import _datetime
        except ImportError:
            pass
        else:
            raise AssertionError('X fallback selection unexpectedly has _datetime')
        assert datetime.date is _pydatetime.date
    public_date, python_date = datetime.date, _pydatetime.date
    date = _pydatetime.date if cpython else datetime.date
    saved_date_new, saved_new = date.__new__, EntryOnly.__new__
    date_code, plain_code = saved_date_new.__code__, saved_new.__code__
    assert metadata(saved_date_new)['has_python_code'] and metadata(saved_new)['has_python_code']
    assert Path(date_code.co_filename).resolve() == PYDATETIME.resolve()
    assert '__init__' not in EntryOnly.__dict__ and EntryOnly.__init__ is object.__init__
    assert EntryOnly.__slots__ == ()

    def check_date(value):
        assert type(value) is date
        assert (value.year, value.month, value.day) == FIELDS
        assert value._getstate() == (STATE,)

    def check_plain(value):
        assert type(value) is EntryOnly and not hasattr(value, '__dict__')

    # One disclosed untimed validation/warm call per case, no monkeypatching.
    check_date(date(STATE))
    check_date(saved_date_new(date, STATE))
    check_plain(EntryOnly(STATE))
    check_plain(saved_new(EntryOnly, STATE))
    iterations = range(OPERATIONS)
    times = {}
    for name in order:
        assert sys.gettrace() is None and sys.getprofile() is None
        if name == 'date_class':
            start = time.perf_counter()
            for _ in iterations:
                value = date(STATE)
            seconds = time.perf_counter() - start
            check_date(value)
        elif name == 'date_saved_new':
            start = time.perf_counter()
            for _ in iterations:
                value = saved_date_new(date, STATE)
            seconds = time.perf_counter() - start
            check_date(value)
        elif name == 'plain_class':
            start = time.perf_counter()
            for _ in iterations:
                value = EntryOnly(STATE)
            seconds = time.perf_counter() - start
            check_plain(value)
        else:
            start = time.perf_counter()
            for _ in iterations:
                value = saved_new(EntryOnly, STATE)
            seconds = time.perf_counter() - start
            check_plain(value)
        assert seconds > 0
        times[name] = seconds
    assert date.__new__ is saved_date_new and EntryOnly.__new__ is saved_new
    assert saved_date_new.__code__ is date_code and saved_new.__code__ is plain_code
    assert datetime.date is public_date and _pydatetime.date is python_date
    assert EntryOnly.__init__ is object.__init__ and '__init__' not in EntryOnly.__dict__
    assert sys.gettrace() is None and sys.getprofile() is None
    after = {p: sha(p) for p in pins}
    assert after == pins
    print(json.dumps(dict(status='artificial_constructor_entry_diagnostic_passed', terminal=True,
        diagnostic_only=True, scored=False, selection=args.selection, case_order=order, seconds=times,
        operations_per_case=OPERATIONS, timed_operation_count=4*OPERATIONS,
        untimed_validation_warm_calls_per_case=1, expected_state_hex=STATE.hex(), expected_fields=list(FIELDS),
        selected_date_is_python_date=date is _pydatetime.date, selected_date_is_public_date=date is datetime.date,
        date_new=metadata(saved_date_new), plain_new=metadata(saved_new),
        original_callable_identities_unchanged=True, plain_slots_empty=True, inherited_object_init_unchanged=True,
        profile_enabled=False, trace_enabled=False, hashes_before=pins, hashes_after=after, hashes_unchanged=True,
        setup_checks_metadata_hashes_excluded_from_timers=True, loop_and_result_replacement_included=True,
        original_pickle_body_run=False, workload_cost_fraction_estimated=False,
        scope='Four artificial allocation loops only; class-call versus saved Python __new__ entry, no suite score, workload share or extrapolated gain'), sort_keys=True), flush=True)


if __name__ == '__main__':
    main()
