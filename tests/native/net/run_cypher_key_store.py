import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    executable, modules = sys.argv[1:3]
    fixture = Path(__file__).with_name('cypher_key_store.py')
    with tempfile.TemporaryDirectory(prefix='xlang3-registry-key-') as directory:
        store = Path(directory)

        def run(mode):
            result = subprocess.run(
                [executable, str(fixture), modules, str(store), mode],
                capture_output=True, text=True, timeout=30)
            if result.returncode or result.stdout.strip() != 'cypher-key-store-' + mode + '-passed':
                raise RuntimeError(f'cypher {mode} failed: {result.stdout}\n{result.stderr}')

        run('create')
        private = store / 'node_test_private_key.pem'
        before = hashlib.sha256(private.read_bytes()).digest()
        modified = private.stat().st_mtime_ns
        # Each call starts a fresh interpreter, with no in-memory key cache.
        run('reload')
        run('reload')
        assert hashlib.sha256(private.read_bytes()).digest() == before
        assert private.stat().st_mtime_ns == modified
        corrupt = store / 'corrupt_private_key.pem'
        corrupt.write_bytes(b'invalid-key-fixture')
        run('invalid')
        assert corrupt.read_bytes() == b'invalid-key-fixture'
        assert not (store / 'missing_private_key.pem').exists()
    print('cypher key persistence passed: restarts, RSA round trips, unchanged key, missing/corrupt export')


if __name__ == '__main__':
    main()
