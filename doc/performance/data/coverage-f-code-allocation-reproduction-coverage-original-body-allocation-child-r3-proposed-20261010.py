"""Untimed original Coverage call; public DLL counters, never a score."""
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
import ctypes
import hashlib
import json
import math
import os
import runpy
import sys

ROOT = 'D:/CantorAI/xlang3'
SITE = ROOT + '/venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages'
BENCH = 'C:/Python/Python314/Lib/site-packages/pyperformance/data-files/benchmarks/bm_coverage/run_benchmark.py'
BENCH_SHA = '19da183fabd314c1374a9bbc541142300669d30d2b9e129d4378dc836b181de6'
DLL = ROOT + '/build-repro/main-verify-20261006/Release/xlang3_runtime.dll'
GETTER = '?xlang_perf_counters@xlang3@@YAAEAUXlangPerfCounters@1@XZ'
SETTER = '?xlang_perf_set_enabled@xlang3@@YAX_N@Z'
CODE, FRAME, KINDS = 44, 45, 50


def layout_probe():
    return None


def main():
    assert sys.version_info[:3] == (3, 14, 7)
    assert sys.platform == 'win32' and ctypes.sizeof(ctypes.c_void_p) == 8
    assert len(sys.argv) == 2 and sys.argv[1] in ('cpython', 'xlang3')
    selection = sys.argv[1]
    assert sys.implementation.name == selection
    if selection == 'cpython':
        assert os.path.normcase(os.path.abspath(sys.executable)) == os.path.normcase('C:/Python/Python314/python.exe')
    sys.path.insert(0, SITE)
    import coverage
    # run_name stays non-main: the original pyperf runner is not invoked here.
    assert hashlib.sha256(open(BENCH, 'rb').read()).hexdigest() == BENCH_SHA
    namespace = runpy.run_path(BENCH, run_name='coverage_allocation_diagnostic_original')
    benchmark = namespace['bench_coverage']
    assert namespace['fibonacci'](10) == 55  # parity only, outside the counter window
    trace_before, profile_before = sys.gettrace(), sys.getprofile()
    assert trace_before is None and profile_before is None
    counts = calibration = layout = None
    setter = snapshot = None
    previous_enabled = None
    if selection == 'xlang3':
        library = ctypes.CDLL(DLL)
        getter, setter = getattr(library, GETTER), getattr(library, SETTER)
        # The actual FFI pointer-return path records the native return address
        # in _address_; indexed access decodes that memory without casting.
        getter.argtypes, getter.restype = [], ctypes.POINTER(ctypes.c_uint64)
        setter.argtypes, setter.restype = [ctypes.c_bool], None
        words = getter()
        assert sys.byteorder == 'little'
        assert ctypes.sizeof(ctypes.c_bool) == 1 and ctypes.sizeof(ctypes.c_uint64) == 8
        # MSVC x64 layout: enabled at byte zero, allocation array at byte8,
        # release array at byte408. Never write through this read-only view.
        def read_enabled():
            return words[0] & 255
        def read_counts():
            return {'Code': [words[1 + CODE], words[1 + KINDS + CODE]],
                    'Frame': [words[1 + FRAME], words[1 + KINDS + FRAME]]}
        snapshot = read_counts
        previous_enabled = bool(read_enabled())
        assert read_enabled() in (0, 1)
        try:
            setter(False)
            assert read_enabled() == 0
            before = snapshot()
            setter(True)
            assert read_enabled() == 1
            probe_code = layout_probe.__code__
            setter(False)
            assert read_enabled() == 0
            after = snapshot()
            assert after['Code'][0] == before['Code'][0] + 1
            assert after['Frame'] == before['Frame']
            calibration = {'excluded_lazy_code_allocations': 1, 'passed': True,
                           'same_process_runtime_counter_bank_verified': True}
            layout = {'kind_count': KINDS, 'Code_index': CODE, 'Frame_index': FRAME,
                      'allocations_offset': 8, 'final_releases_offset': 408,
                      'allocations_word_indices': {'Code': 45, 'Frame': 46},
                      'final_releases_word_indices': {'Code': 95, 'Frame': 96},
                      'uint64_bytes': 8, 'enabled_flag_toggle_verified': True,
                      'access': 'direct POINTER(c_uint64) FFI return plus indexed reads'}
        finally:
            setter(previous_enabled)
            assert bool(read_enabled()) == previous_enabled
    # All imports, pointer preparation and the disclosed calibration precede
    # this single unchanged original function call. No resets or extra hooks.
    try:
        if setter is not None:
            before = snapshot()
            setter(True)
        try:
            returned = benchmark(1)
        finally:
            if setter is not None:
                setter(False)
                after = snapshot()
                setter(previous_enabled)
        assert isinstance(returned, float) and math.isfinite(returned) and returned > 0
        observed_trace, observed_profile = sys.gettrace(), sys.getprofile()
        assert observed_trace is trace_before and observed_profile is profile_before
        if setter is not None:
            assert bool(read_enabled()) == previous_enabled
            counts = {name: {'allocations': after[name][0] - before[name][0],
                             'final_releases': after[name][1] - before[name][1]}
                      for name in ('Code', 'Frame')}
            assert all(value >= 0 for row in counts.values() for value in row.values())
    finally:
        if setter is not None:
            setter(previous_enabled)
        sys.settrace(trace_before)
        sys.setprofile(profile_before)
    print(json.dumps({'status': 'completed_untimed_original_coverage_allocation_diagnostic',
        'selection': selection, 'scored': False, 'bench_coverage_calls': 1, 'loops': 1,
        'benchmark_sha256': BENCH_SHA, 'coverage_version': coverage.__version__,
        'coverage_file': coverage.__file__, 'fibonacci_10': 55, 'return_duration_valid': True,
        'trace_restored': True, 'profile_restored': True, 'counter_enabled_restored': True,
        'counter_reset': False, 'requested_runtime_path': DLL if selection == 'xlang3' else None, 'layout': layout,
        'excluded_calibration': calibration, 'counts': counts,
        'scope': 'Whole original bench_coverage(1) invocation after imports, including Coverage start/stop. '
                 'Counts are diagnostic only; duration is not reported or scored.'}, sort_keys=True))


if __name__ == '__main__':
    main()
