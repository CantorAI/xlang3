"""One unchanged official pprint safe_repr body; diagnostic only, never scored.

Default has no profiler. --profile explicitly changes shortcut eligibility;
its optional Python call counts describe only that instrumented execution.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import pprint
import runpy
import sys
import time


SOURCE = Path('C:/Python/Python314/Lib/site-packages/pyperformance/data-files/benchmarks/bm_pprint/run_benchmark.py')
SOURCE_SHA = '07ef57201c7919aedf9e340c7fb1561a40dfee698818c9694930f3e427091d6d'
PPRINT_SHA = 'c29eb77af95120a7e9b107b6dc3cf093fcbe37b22aa27441cf58cacbeb56904e'


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def emit(row):
    print(json.dumps(row, sort_keys=True), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--benchmark-script', type=Path, default=SOURCE)
    parser.add_argument('--profile', action='store_true')
    args = parser.parse_args()
    if sys.flags.optimize != 0:
        raise RuntimeError("Original diagnostic requires optimization level zero")
    if sys.implementation.name == 'cpython':
        assert sys.version_info[:3] == (3, 14, 7), 'Use exact CPython 3.14.7'
    source = args.benchmark_script.resolve(strict=True)
    library = Path(pprint.__file__).resolve(strict=True)
    tracked = {str(source): SOURCE_SHA, str(library): PPRINT_SHA}
    assert all(digest(path) == sha for path, sha in tracked.items()), 'Source/library drift'
    # The official module creates the original printer and aliased 100000-item
    # list. Its __main__ Runner block is not executed or replaced.
    namespace = runpy.run_path(str(source), run_name='pprint_original_safe_repr_diagnostic')
    printable, printer = namespace['printable'], namespace['p']
    assert len(printable) == 100000 and printable[0] is printable[-1]
    assert namespace['PrettyPrinter'] is pprint.PrettyPrinter
    function = printer._safe_repr
    prechecks = {'sort_dicts': printer._sort_dicts,
        'dict_repr_identity': getattr(type(printable[0][3]), '__repr__', None) is dict.__repr__,
        'list_repr_identity': getattr(type(printable), '__repr__', None) is list.__repr__,
        'tuple_repr_identity': getattr(type(printable[0]), '__repr__', None) is tuple.__repr__}
    counts = {name: 0 for name in ('_safe_repr', 'format', '_safe_tuple', '_safe_key.__lt__')}
    codes = ((pprint.PrettyPrinter._safe_repr.__code__, '_safe_repr'),
             (pprint.PrettyPrinter.format.__code__, 'format'),
             (pprint._safe_tuple.__code__, '_safe_tuple'),
             (pprint._safe_key.__lt__.__code__, '_safe_key.__lt__')) if args.profile else ()

    def profile(frame, event, arg):
        if event == 'call':
            code = frame.f_code
            for target, name in codes:
                if code is target:
                    counts[name] += 1
                    break

    metadata = {'purpose': 'one original pprint safe_repr invocation; unscored',
        'runtime': sys.implementation.name, 'version': sys.version,
        'version_info': list(sys.version_info[:3]), 'executable': sys.executable,
        'pid': os.getpid(), 'benchmark_source': str(source), 'benchmark_source_sha256': SOURCE_SHA,
        'pprint_source': str(library), 'pprint_source_sha256': PPRINT_SHA,
        'input_length': len(printable), 'input_alias_preserved': printable[0] is printable[-1],
        'repeat': 1, 'prechecks': prechecks, 'optimization_level': sys.flags.optimize,
        'profile_enabled': args.profile, 'instrumented_shortcuts_changed': args.profile,
        'profile_scope': 'four pprint Python code identities only; no duration attribution',
        'elapsed_boundary': 'exactly p._safe_repr(printable, {}, None, 0)',
        'tracked_source_sha256_before': tracked}
    assert sys.getprofile() is None, 'Do not replace an existing profiler'
    assert sys.gettrace() is None, 'Do not inherit a trace function'
    emit(dict(metadata, status='body_start', terminal=False, success=None))
    result = None
    original_failure = None
    elapsed = None
    try:
        if args.profile:
            sys.setprofile(profile)
        started = time.perf_counter()
        try:
            result = function(printable, {}, None, 0)
        except BaseException as error:
            original_failure = error
            raise
        finally:
            try:
                elapsed = time.perf_counter() - started
            except BaseException:
                if original_failure is None:
                    raise
    except BaseException as error:
        # Keep the actual exception alive and bare-reraise it. Reporting or
        # profiler cleanup must not replace its identity/traceback/pending path.
        if original_failure is None:
            original_failure = error
        raise
    finally:
        try:
            if args.profile:
                sys.setprofile(None)
            after = {path: digest(path) if Path(path).is_file() else None for path in tracked}
            stable = after == tracked
            signature = None
            if original_failure is None:
                assert type(result) is tuple and len(result) == 3
                assert type(result[0]) is str and type(result[1]) is bool and type(result[2]) is bool
                signature = {'tuple_length': 3, 'text_length': len(result[0]),
                    'text_utf8_sha256': hashlib.sha256(result[0].encode('utf-8')).hexdigest(),
                    'readable': result[1], 'recursive': result[2]}
            emit(dict(metadata, status='body_complete' if original_failure is None and stable else 'body_failed',
                terminal=True, success=original_failure is None and stable,
                failure_type=type(original_failure).__name__ if original_failure is not None else None,
                elapsed_seconds_diagnostic_only=elapsed, result_signature=signature,
                tracked_source_sha256_after=after, hashes_unchanged=stable,
                profile_counts=counts if args.profile else None))
            if original_failure is None:
                assert stable, 'Source/library changed during body'
        except BaseException:
            if original_failure is None:
                raise
            # The original bare raise remains authoritative in stderr. Preserve
            # raw bytes even when a secondary reporting operation itself fails.
            try:
                print('Diagnostic reporting failed while preserving original body exception', file=sys.stderr, flush=True)
            except BaseException:
                pass


if __name__ == '__main__':
    main()
