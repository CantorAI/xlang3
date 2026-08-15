<!--
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->
# pyperformance Track

`pyperformance` is the reference benchmark suite used by the Python ecosystem to compare Python implementations and CPython versions. XLang3 keeps its small microbenchmarks for VM tuning, and uses this folder to track progress toward larger Python-compatible benchmarks.

Official resources:

- https://pyperformance.readthedocs.io/
- https://github.com/python/pyperformance
- https://speed.python.org/
- https://github.com/faster-cpython/bench_runner

Current policy:

- Microbenchmarks in `benchmarks/cases` are for executor and IR tuning.
- `pyperformance` benchmarks are for Python compatibility and real application behavior.
- A benchmark should only be added to `supported.txt` after it runs unchanged on both CPython and XLang3.
- Unsupported benchmarks should stay unsupported rather than being rewritten into a synthetic XLang3-only form.

Install `pyperformance` for the CPython side:

```powershell
python -m pip install -r .\benchmarks\pyperformance\requirements.txt
```

List available pyperformance benchmarks:

```powershell
python .\benchmarks\pyperformance\run.py list --python python
```

Show XLang3 support status:

```powershell
python .\benchmarks\pyperformance\run.py status
```

Run selected benchmarks on CPython:

```powershell
python .\benchmarks\pyperformance\run.py run-cpython --python python --benchmark json_dumps --output pyperformance-cpython.json
```

When XLang3 supports enough Python and stdlib for a pyperformance case, add the benchmark name to `supported.txt` and add a note explaining any environment requirements.
