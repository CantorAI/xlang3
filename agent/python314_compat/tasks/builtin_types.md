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
# Builtin Type Tasks

- [x] containers
  Coverage: `tests/fixtures/compat_sections/containers.py`
  Remaining: none in the current scoped audit.

- [~] strings and Unicode
  Coverage: `tests/fixtures/compat_sections/strings_and_unicode.py`
  Remaining: generated Unicode database, normalization completeness, locale casing, grapheme segmentation, identifier edge cases, and full codec registry behavior.

- [~] bytes, bytearray, and memoryview
  Coverage: `tests/fixtures/core/binary_buffers.py`, `tests/fixtures/compat_sections/builtins.py`
  Remaining: full buffer protocol parity and memoryview format/cast/release edge cases.

