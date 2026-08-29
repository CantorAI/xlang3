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
# Filesystem And IO Tasks

- [~] VFS abstraction
  Coverage: `tests/fixtures/core/vfs_file_io.py`
  Remaining: unify host filesystem, embedded filesystem, and import file reads through one VFS contract.

- [~] file object and open
  Coverage: `tests/fixtures/core/file_context_open.py`, `tests/fixtures/core/file_io_compat.py`
  Remaining: exact buffering, newline translation, fd-backed files, opener callbacks, and platform error classes.

- [~] os fd API
  Coverage: `tests/fixtures/compat_sections/standard_modules.py`
  Remaining: `os.open`, `os.close`, `os.read`, `os.write`, `os.lseek`, `os.fstat`, inheritability, and Windows flag mapping.

