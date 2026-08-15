# XLang3 Tests

The test tree is arranged by checkpoint layer.

- `cpp/`: C++ unit tests for internal parser, runtime, IR lowering, and interpreter behavior.
- `fixtures/core/`: `.py` programs that must run through the public CLI.
- `fixtures/expected/`: expected stdout for fixture programs.
- `run_fixtures.ps1`: Windows fixture runner used by CTest.
- `sdk_c_header_smoke.c`: C-only public SDK header check.

Checkpoint rule: before growing the language surface, Release and Debug builds should pass CTest.

Current checkpoint:

```text
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Function work can proceed after this checkpoint stays green. Tuple should be added before Python-compatible varargs or argument unpacking.
