import importlib.util
from pathlib import Path
from tempfile import TemporaryDirectory


with TemporaryDirectory() as directory:
    source = Path(directory) / 'loaded_module.py'
    source.write_text('answer = 42\n')
    spec = importlib.util.spec_from_file_location('loaded_module', source)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    print(module.answer, spec.origin == str(source))
