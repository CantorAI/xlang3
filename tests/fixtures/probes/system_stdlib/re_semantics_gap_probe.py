# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.

import re

cases = [
    ("named", lambda: re.search(r"(?P<word>[a-z]+)-(?P=word)", "abc-abc").group("word")),
    ("lookahead", lambda: re.search(r"foo(?=bar)", "foobar").group(0)),
    ("neg-lookahead", lambda: re.search(r"foo(?!bar)", "foobaz").group(0)),
    ("pos-lookbehind", lambda: re.search(r"(?<=foo)bar", "foobar").group(0)),
    ("neg-lookbehind", lambda: re.search(r"(?<!foo)bar", "xxbar").group(0)),
    ("nongreedy", lambda: re.search(r"a.*?b", "axxbxxb").group(0)),
    ("alternation", lambda: re.search(r"cat|dog", "a dog").group(0)),
    ("word-boundary", lambda: re.search(r"\bword\b", "a word!").group(0)),
    ("sub-backref", lambda: re.sub(r"([a-z]+)([0-9]+)", r"\2-\1", "id42")),
    ("finditer-pos", lambda: [(m.group(0), m.start()) for m in re.finditer(r"\d+", "a12b3")]),
]
for name, fn in cases:
    try:
        print(name, fn())
    except Exception as exc:
        print(name, type(exc).__name__, str(exc)[:80])
