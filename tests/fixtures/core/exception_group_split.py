# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0


group = ExceptionGroup(
    "outer",
    [ValueError("one"), ExceptionGroup("inner", [TypeError("two"), ValueError("three")])],
)

matching, remaining = group.split(ValueError)
print(matching.message, len(matching.exceptions), type(matching.exceptions[1]).__name__)
print(remaining.message, len(remaining.exceptions), type(remaining.exceptions[0]).__name__)

callable_matching, callable_remaining = group.split(lambda exc: isinstance(exc, TypeError))
print(callable_matching.message, str(callable_matching.exceptions[0].exceptions[0]))
print(len(callable_remaining.exceptions))

print(group.subgroup(KeyError) is None)
print(group.subgroup(Exception) is group)
