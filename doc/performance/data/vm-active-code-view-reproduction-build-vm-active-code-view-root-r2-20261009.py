"""Execute the frozen builder with a separately authenticated test-harness layer."""
import hashlib
import importlib.util
import json
from pathlib import Path
import sys

ROOT = Path('D:/CantorAI/xlang3')
PARENT = ROOT / 'scratch/performance/build-vm-active-code-view-proposed-20261009.py'
PARENT_SHA = '120b53c7ffbe3788abf3224c08e16b6a765db8fb99085164fb96a6aaf794c0c1'
HARNESS_SHA = 'cb04a01c52c2fc406816e1cb63896c4f463e414a354ebf62dc845bf77ca4f38f'


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def authenticate_harness(app, control):
    path = ROOT / 'doc/performance/data/gc-r7b-full-ctest-harness-repair-20261009-receipt.json'
    assert Path(app['harness_repair_receipt_path']).resolve() == path.resolve()
    assert sha(path) == app['harness_repair_receipt_sha256'] == HARNESS_SHA
    receipt = json.loads(path.read_bytes())
    assert receipt['terminal'] and receipt['passed'] and receipt['exit_code'] == 0
    assert receipt['registered_count'] == 55 and len(set(receipt['passed_names'])) == 55
    assert receipt['cleanup_completed'] and receipt['hashes_unchanged']
    repair = receipt['harness_source_sha256']
    assert set(repair) == {'tests/run_fixtures.ps1', 'tests/cli/run_debugpy_launch_smoke.py'}
    assert app['harness_repair_source_sha256'] == repair
    protected = {'tests/cli/run_debugpy_launch_smoke.py': repair['tests/cli/run_debugpy_launch_smoke.py']}
    assert app['protected_test_input_sha256'] == protected
    for name, value in protected.items():
        assert sha(ROOT / name) == value
    for name, value in repair.items():
        assert receipt['pins'][str(ROOT / name)] == value
    for name, value in control['release_sha256'].items():
        assert receipt['pins'][str(ROOT / 'build-repro/main-verify-20261006/Release' / name)] == value
    for label in ('stdout', 'stderr'):
        assert sha(path.parent / receipt[label]) == receipt[label + '_sha256']
    return {'harness_repair_receipt_path': str(path), 'harness_repair_receipt_sha256': HARNESS_SHA,
            'harness_repair_source_sha256': repair, 'protected_test_input_sha256': protected}


def main():
    assert sha(PARENT) == PARENT_SHA
    assert sys.version_info[:3] == (3, 14, 7) and not sys.flags.optimize
    assert Path(sys.executable).resolve() == Path('C:/Python/Python314/python.exe').resolve()
    application = Path(sys.argv[sys.argv.index('--application') + 1])
    prefix = sys.argv[sys.argv.index('--prefix') + 1]
    app = json.loads(application.read_bytes())
    control = json.loads(Path(app['accepted_control_manifest_path']).read_bytes())
    layer = authenticate_harness(app, control)
    spec = importlib.util.spec_from_file_location('frozen_vm_builder', PARENT)
    parent = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(parent)
    result = parent.main()
    receipt_path = ROOT / 'doc/performance/data' / (prefix + '-build.json')
    receipt = json.loads(receipt_path.read_bytes())
    receipt['underlying_controller_sha256'] = receipt['controller_sha256']
    receipt['controller_sha256'] = sha(__file__)
    try:
        assert authenticate_harness(app, control) == layer
        receipt.update(layer, harness_layer_unchanged=True)
    except BaseException as error:
        receipt.update(passed=False, status='build_failed_or_invalid', harness_layer_unchanged=False,
                       harness_layer_error=repr(error))
        result = 1
    receipt_path.write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8', newline='\n')
    print(json.dumps({'root_build_status': receipt['status'], 'receipt_sha256': sha(receipt_path)}), flush=True)
    return result


if __name__ == '__main__':
    raise SystemExit(main())
