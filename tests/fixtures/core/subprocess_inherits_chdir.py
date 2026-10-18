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
        # macOS temporary paths may use /var while getcwd resolves /private/var.
        print(os.path.normcase(os.path.realpath(child)) ==
              os.path.normcase(os.path.realpath(directory)))

        explicit = subprocess.check_output(
            [sys.executable, '-c', 'import os; print(os.getcwd())'],
            cwd=original, text=True
        ).strip()
        print(os.path.normcase(os.path.realpath(explicit)) ==
              os.path.normcase(os.path.realpath(original)))
    finally:
        os.chdir(original)
