# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0.
"""Root-only, unscored compile-IR observation of one unchanged raytrace body.

The CLI dumps before execution. Completion warms original call sites, but this
artifact does not expose adaptive call-cache kinds or prove inliner admission.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path('D:/CantorAI/xlang3')
CP = Path('C:/Python/Python314/python.exe')
RELEASE = ROOT / 'build-repro/main-verify-20261006/Release'
EXE = RELEASE / 'xlang3.exe'
DLL = RELEASE / 'xlang3_runtime.dll'
SOURCE = CP.parent / 'Lib/site-packages/pyperformance/data-files/benchmarks/bm_raytrace/run_benchmark.py'
SOURCE_SHA = '88ef4d9060d8e8f6ce40f376477aaf89cc808fa44813225a3071a05a1467f017'
SITE = ROOT / 'venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages'
HOOK = ROOT / 'benchmarks/diagnostics/pyperf_compat/sitecustomize.py'
HOOK_SHA = '2ffc217fe20497156bfcb1cebc7fb87203f33c6d4d3dbb6ecbcbcf91ea4cb317'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def release_tree():
    return {p.relative_to(RELEASE).as_posix(): sha(p)
            for p in sorted(RELEASE.rglob('*')) if p.is_file()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe-sha256', required=True)
    parser.add_argument('--dll-sha256', required=True)
    parser.add_argument('--prefix', default='raytrace-original-worker-ir-20261009')
    args = parser.parse_args()
    if (sys.implementation.name != 'cpython' or sys.version_info[:3] != (3, 14, 7)
            or sys.flags.optimize or Path(sys.executable).resolve() != CP.resolve()):
        raise RuntimeError('Use unoptimized C:/Python/Python314/python.exe 3.14.7')
    if sys.gettrace() is not None or sys.getprofile() is not None:
        raise RuntimeError('Manager trace/profile must be disabled')
    if not re.fullmatch(r'raytrace-original-worker-ir(?:-r[2-9][0-9]*)?-20261009', args.prefix):
        raise RuntimeError('Use the fixed fresh observation prefix or a distinct R2+ prefix')
    output = ROOT / 'scratch/performance' / args.prefix
    output.mkdir(exist_ok=False)
    receipt = output / 'receipt.json'
    stdout, stderr = output / 'stdout.log', output / 'stderr.log'
    result_path, ir_path = output / 'worker.pyperf.json', output / 'run_benchmark.ir.txt'
    record = {'terminal': False, 'status': 'preflight', 'timing_scoring_permitted': False,
              'compile_ir_only': True, 'adaptive_cache_eligibility_observed': False,
              'original_source': str(SOURCE), 'original_source_sha256': SOURCE_SHA,
              'body': 'bench_raytrace(1, 100, 100, None)', 'original_body_calls_requested': 1,
              'trace_profile_policy': 'fresh worker; no trace/profile/debug/hook options; standard pinned compatibility hook only',
              'trace_profile_state_in_worker_observed': False,
              'release_before': {}, 'inputs_before': {}, 'raw': {}}
    child = None
    pins = {}
    before = {}
    try:
        if sha(EXE) != args.exe_sha256 or sha(DLL) != args.dll_sha256:
            raise RuntimeError('Restored fixed-path executable/runtime identity mismatch')
        if sha(SOURCE) != SOURCE_SHA or sha(HOOK) != HOOK_SHA:
            raise RuntimeError('Original source or compatibility hook changed')
        files = [Path(__file__).resolve(), SOURCE, HOOK, CP, CP.with_name('python314.dll'),
                 ROOT / 'src/xlang3.cpp', ROOT / 'src/ir/dump.cpp',
                 ROOT / 'src/executor/xlang_vm/ops/xlang_vm_ops_call.h',
                 ROOT / 'src/executor/xlang_vm/xlang_vm_inline_support.h']
        files.extend(sorted((SITE / 'pyperf').rglob('*.py')))
        if not (SITE / 'pyperf/_runner.py').is_file() or not (SITE / 'pyperf/_worker.py').is_file():
            raise RuntimeError('Required original dependency site missing')
        pins = {str(p): sha(p) for p in files}
        before = release_tree()
        if len(before) != 178:
            raise RuntimeError('Unexpected complete Release file count')
        record.update(inputs_before=pins, release_before=before)
        env = os.environ.copy()
        for name in tuple(env):
            if (name.startswith('PYTHON') or name.startswith('XLANG3_')
                    or name in ('_NT_SYMBOL_PATH', '_NT_ALT_SYMBOL_PATH')):
                env.pop(name, None)
        env.update(PYTHONPATH=os.pathsep.join((str(HOOK.parent), str(SITE))),
                   XLANG3_PYTHON_LIB=str(CP.parent / 'Lib'), PYTHONIOENCODING='utf-8',
                   PYTHONUNBUFFERED='1', PYTHONDONTWRITEBYTECODE='1')
        command = [str(EXE), '--dump-ir', '--debug-dir', str(output), str(SOURCE),
                   '--worker', '--worker-task', '0', '--loops', '1', '--values', '1',
                   '--warmups', '0', '--width', '100', '--height', '100',
                   '--output', str(result_path)]
        record.update(command=command, child_environment={k: env[k] for k in
                      ('PYTHONPATH', 'XLANG3_PYTHON_LIB', 'PYTHONIOENCODING', 'PYTHONUNBUFFERED', 'PYTHONDONTWRITEBYTECODE')},
                      status='running')
        receipt.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
        with stdout.open('wb') as out, stderr.open('wb') as err:
            child = subprocess.Popen(command, cwd=ROOT, env=env, stdin=subprocess.DEVNULL,
                                     stdout=out, stderr=err, creationflags=subprocess.CREATE_NO_WINDOW)
            record['pid'] = child.pid
            record['exit_code'] = child.wait(timeout=300)
        if record['exit_code'] != 0:
            raise RuntimeError('Original worker failed; retain raw output and any compile IR')
        document = json.loads(result_path.read_text(encoding='utf-8'))
        benches = document['benchmarks']
        if len(benches) != 1 or benches[0].get('metadata', {}).get('name', document.get('metadata', {}).get('name')) != 'raytrace':
            raise RuntimeError('Unexpected original worker benchmark identity')
        runs = benches[0]['runs']
        if len(runs) != 1 or len(runs[0].get('values', [])) != 1 or runs[0].get('warmups', []):
            raise RuntimeError('Expected one original body value and zero warmups')
        metadata = {**document.get('metadata', {}), **benches[0].get('metadata', {}), **runs[0].get('metadata', {})}
        if metadata.get('loops') != 1 or metadata.get('raytrace_width') != 100 or metadata.get('raytrace_height') != 100:
            raise RuntimeError('Original body loop/dimension metadata mismatch')
        blocks = re.split(r'(?=^function #)', ir_path.read_text(encoding='utf-8'), flags=re.M)
        matches = [b for b in blocks if re.search(r'^  qualname: Vector\.dot$', b, re.M)]
        if len(matches) != 1:
            raise RuntimeError('Expected one original Vector.dot IR function')
        dot = matches[0]
        if '  first_line: 51\n' not in dot or '  params: %0=self %1=other\n' not in dot:
            raise RuntimeError('Vector.dot source/signature association mismatch')
        dot_path = output / 'Vector.dot.ir.txt'
        dot_path.write_text(dot, encoding='utf-8')
        record.update(status='observation_passed', original_worker_completed=True,
                      dot_ir_path=str(dot_path), dot_ir_sha256=sha(dot_path),
                      eligibility_limit='Source admission requires zero explicit args and one self parameter; this two-parameter dot IR is outside that route. No cache kind/frame-count observation.')
    except Exception as error:
        record.update(status='observation_failed', error=repr(error))
    finally:
        try:
            if child is not None and child.poll() is None:
                child.kill()
                child.wait(timeout=30)
            record['cleanup_completed'] = child is None or child.poll() is not None
        except Exception as error:
            record.update(status='observation_failed', cleanup_completed=False, cleanup_error=repr(error))
        for path in (stdout, stderr, result_path, ir_path):
            if path.is_file():
                record['raw'][str(path)] = sha(path)
        if pins:
            record['inputs_after'] = {p: sha(Path(p)) for p in pins}
            record['inputs_unchanged'] = record['inputs_after'] == pins
        if before:
            record['release_after'] = release_tree()
            record['release_unchanged'] = record['release_after'] == before
        if not record.get('inputs_unchanged') or not record.get('release_unchanged'):
            record['status'] = 'observation_failed'
        record['terminal'] = True
        receipt.write_text(json.dumps(record, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({'receipt': str(receipt), 'receipt_sha256': sha(receipt), 'status': record['status']}))
    return 0 if record['status'] == 'observation_passed' else 1


if __name__ == '__main__':
    raise SystemExit(main())
