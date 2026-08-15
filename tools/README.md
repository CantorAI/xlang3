# XLang3 Tools

This directory is for developer tools used to build, validate, or inspect XLang3.

Initial planned tools:

- `parsergen/`: generate the Python-compatible parser from the checked-in grammar.
- `grammarcheck/`: validate grammar changes and parser test fixtures.
- `irdump/`: print program IR and graph IR in a stable debug format.
- `abicheck/`: validate native extension ABI headers and generated bindings.

Tools should be build-time or developer-time dependencies. The normal XLang3 runtime should not depend on parser generator tooling, LLVM, or other large toolchain packages unless a specific executor mode requires them.
