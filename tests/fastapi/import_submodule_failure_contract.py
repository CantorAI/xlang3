import json
import sys
import tempfile
from pathlib import Path

from fastapi import FastAPI
from fastapi.testclient import TestClient


app = FastAPI()


@app.get('/import-errors')
def import_errors():
    with tempfile.TemporaryDirectory() as directory:
        package = Path(directory) / 'xlang3_http_import_failure_probe'
        package.mkdir()
        (package / '__init__.py').write_text(
            "def __getattr__(name):\n"
            "    if name == 'answer':\n"
            "        return 42\n"
            "    if name == 'explode':\n"
            "        raise ValueError('lazy attribute failure')\n"
            "    raise AttributeError(name)\n",
            encoding='utf-8',
        )
        (package / 'broken.py').write_text(
            "raise ValueError('submodule body failure')\n", encoding='utf-8'
        )
        (package / 'missing_dependency.py').write_text(
            'import xlang3_http_import_failure_probe_missing_dependency\n',
            encoding='utf-8',
        )
        sys.path.insert(0, directory)
        try:
            from xlang3_http_import_failure_probe import answer

            errors = {'answer': answer}
            for name in ('broken', 'missing_dependency'):
                try:
                    exec(f'from xlang3_http_import_failure_probe import {name}')
                except Exception as error:
                    errors[name] = [type(error).__name__, str(error)]
            try:
                from xlang3_http_import_failure_probe import explode
            except Exception as error:
                errors['explode'] = [type(error).__name__, str(error)]
            return errors
        finally:
            sys.path.remove(directory)


response = TestClient(app).get('/import-errors')
print(response.status_code, json.dumps(response.json(), sort_keys=True))
