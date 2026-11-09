# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# SPDX-License-Identifier: Apache-2.0

import demo

answer = demo.answer()
parsed = demo.parse_yaml("answer: 42")
assert answer == 42
assert parsed == {"answer": 42}
print(answer)
print(parsed)
