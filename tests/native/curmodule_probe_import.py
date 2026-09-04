# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.

import xlang_curmodule_probe as probe

name = probe.curmodule_name()
path = probe.curmodule_file()

if name != "__main__":
    raise RuntimeError("bad curModule __name__: " + name)

if "curmodule_probe_import.py" not in path:
    raise RuntimeError("bad curModule __file__: " + path)

print("curModule ok")
