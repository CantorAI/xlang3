"""Diagnose Chameleon Python method binding; does not produce a timing score."""
import runpy
import sys
from pathlib import Path

sys.path.insert(0, str(Path("venv/cpython3.14-a6792301b742-compat-31b33d68c68a/Lib/site-packages").resolve()))
from chameleon.utils import Scope

scope = Scope({"options": {"table": []}})
method = scope.get_name
print("method type", type(method).__name__)
print("bound receiver", getattr(method, "__self__", None) is scope)
print("direct", scope.get_name("options"))
print("saved", method("options"))

benchmark = runpy.run_path(r"C:\Python\Python314\Lib\site-packages\pyperformance\data-files\benchmarks\bm_chameleon\run_benchmark.py", run_name="diagnostic")
template = benchmark["PageTemplate"](benchmark["BIGTABLE_ZPT"], keep_source=True)
output = Path(sys.argv[1])
output.write_text(template.source, encoding="utf-8")
print("generated source", str(output))
print("render", len(template(options={"table": []})))
