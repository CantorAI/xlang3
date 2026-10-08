"""One original pickle_pure_python function invocation; unscored diagnostic.

The pinned official body remains Python and is called once, without replacing
its dumps function, objects, unrolled operations, protocol, or timer.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import runpy
import sys
import time
from types import SimpleNamespace

SOURCE = Path('C:/Python/Python314/Lib/site-packages/pyperformance/data-files/benchmarks/bm_pickle/run_benchmark.py')
SOURCE_SHA = '31c0e30be79514b45db0d4e858632eb429b0fea3edbac6463ad5f34d92f3cab8'
PICKLE_SHA = '144fdf593814627e13a5fcb0947728e8f0775ecdec181046ee4609063cf232ec'
OUTER_LOOPS = 41
PROTOCOL = 5

def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def emit(row):
    print(json.dumps(row, sort_keys=True), flush=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--benchmark-script', type=Path, default=SOURCE)
    args = parser.parse_args()
    if sys.flags.optimize != 0:
        raise RuntimeError('Original diagnostic requires optimization level zero')
    if sys.implementation.name == 'cpython':
        assert sys.version_info[:3] == (3, 14, 7), 'Use exact CPython3.14.7'
    assert sys.getprofile() is None and sys.gettrace() is None
    source = args.benchmark_script.resolve(strict=True)
    assert digest(source) == SOURCE_SHA
    # Official setup creates DICT, TUPLE, and three seeded mutated dictionaries
    # before its Runner selects the pure-Python pickle import below.
    namespace = runpy.run_path(str(source), run_name='pickle_original_body_diagnostic')
    assert 'pickle' not in sys.modules, 'Fresh process must not preload pickle'
    sys.modules['_pickle'] = None
    import pickle
    library = Path(pickle.__file__).resolve(strict=True)
    assert digest(library) == PICKLE_SHA
    tracked = {str(source): SOURCE_SHA, str(library): PICKLE_SHA}
    names = ('DICT', 'TUPLE', 'DICT_GROUP')
    objects = tuple(namespace[name] for name in names)
    function, inner_loops = namespace['BENCHMARKS']['pickle']
    assert function is namespace['bench_pickle'] and inner_loops == 20
    prechecks = {
        'pickler_is_python': pickle.Pickler is pickle._Pickler,
        'unpickler_is_python': pickle.Unpickler is pickle._Unpickler,
        'dumps_is_python': pickle.dumps is pickle._dumps,
        'loads_is_python': pickle.loads is pickle._loads,
        'pickle_accelerator_blocked': sys.modules['_pickle'] is None,
        'highest_protocol_five': pickle.HIGHEST_PROTOCOL == PROTOCOL,
        'pickler_module_pickle': pickle.Pickler.__module__ == 'pickle',
        'dumps_module_pickle': pickle.dumps.__module__ == 'pickle',
        'dict_group_three_distinct': len(objects[2]) == 3 and objects[2][0] is not objects[2][1] and objects[2][1] is not objects[2][2],
    }
    assert all(prechecks.values()), prechecks
    dumps, loads, pickler = pickle.dumps, pickle.loads, pickle.Pickler
    # Audit is outside the workload and never wraps/replaces dumps. Byte digest
    # and pure-Python roundtrip checks are compared to the fresh CP reference.
    def audit():
        rows = []
        for name, obj in zip(names, objects):
            data = dumps(obj, PROTOCOL)
            assert type(data) is bytes and data[:2] == b'\x80\x05'
            decoded = loads(data)
            rows.append({'name': name, 'byte_length': len(data),
                'sha256': hashlib.sha256(data).hexdigest(), 'roundtrip_equal': decoded == obj,
                'root_type': type(obj).__name__})
            assert rows[-1]['roundtrip_equal']
        return rows
    signature_before = audit()
    metadata = {'purpose': 'one unchanged original pickle_pure_python body; unscored',
        'runtime': sys.implementation.name, 'version': sys.version,
        'version_info': list(sys.version_info[:3]), 'executable': sys.executable,
        'pid': os.getpid(), 'benchmark_source': str(source), 'benchmark_source_sha256': SOURCE_SHA,
        'pickle_source': str(library), 'pickle_source_sha256': PICKLE_SHA,
        'outer_loops': OUTER_LOOPS, 'inner_loops_metadata': inner_loops,
        'object_count': 3, 'dumps_per_object_per_outer_loop': 20,
        'total_dumps_in_original_body': OUTER_LOOPS * 3 * 20,
        'protocol': PROTOCOL, 'repeat': 1, 'prechecks': prechecks,
        'optimization_level': sys.flags.optimize, 'profile_enabled': False,
        'elapsed_boundary': 'original bench_pickle(41, pickle, options(protocol=5)) once',
        'signature_before': signature_before, 'tracked_source_sha256_before': tracked}
    emit(dict(metadata, status='body_start', terminal=False, success=None))
    original_failure = None
    elapsed = original_elapsed = None
    signature_after = None
    try:
        started = time.perf_counter()
        try:
            original_elapsed = function(OUTER_LOOPS, pickle, SimpleNamespace(protocol=PROTOCOL))
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
        if original_failure is None:
            original_failure = error
        raise
    finally:
        try:
            after = {path: digest(path) if Path(path).is_file() else None for path in tracked}
            stable = after == tracked
            if original_failure is None:
                signature_after = audit()
                assert signature_after == signature_before
                assert all(namespace[name] is obj for name, obj in zip(names, objects))
                assert pickle.dumps is dumps and pickle.loads is loads and pickle.Pickler is pickler
                assert sys.modules['_pickle'] is None and sys.getprofile() is None and sys.gettrace() is None
                assert isinstance(original_elapsed, float) and original_elapsed >= 0
            emit(dict(metadata, status='body_complete' if original_failure is None and stable else 'body_failed',
                terminal=True, success=original_failure is None and stable,
                failure_type=type(original_failure).__name__ if original_failure is not None else None,
                elapsed_seconds_diagnostic_only=elapsed,
                original_timer_seconds_diagnostic_only=original_elapsed,
                signature_after=signature_after, input_and_implementation_identity_preserved=original_failure is None,
                tracked_source_sha256_after=after, hashes_unchanged=stable,
                sampling_boundary_limit='body_complete follows three post-body audit dumps/loads; sampling can include this small reporting tail'))
            if original_failure is None:
                assert stable, 'Source/library changed during body'
        except BaseException:
            if original_failure is None:
                raise
            try:
                print('Diagnostic reporting failed while preserving original body exception', file=sys.stderr, flush=True)
            except BaseException:
                pass

if __name__ == '__main__':
    main()
