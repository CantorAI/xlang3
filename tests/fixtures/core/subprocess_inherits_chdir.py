import os
import subprocess
import sys
import tempfile


original = os.getcwd()
with tempfile.TemporaryDirectory() as directory:
    os.chdir(directory)
    try:
        child = subprocess.check_output(
            [sys.executable, '-c', 'import os; print(os.getcwd())'], text=True
        ).strip()
        print(os.path.normcase(os.path.abspath(child)) ==
              os.path.normcase(os.path.abspath(directory)))

        explicit = subprocess.check_output(
            [sys.executable, '-c', 'import os; print(os.getcwd())'],
            cwd=original, text=True
        ).strip()
        print(os.path.normcase(os.path.abspath(explicit)) ==
              os.path.normcase(os.path.abspath(original)))
    finally:
        os.chdir(original)
