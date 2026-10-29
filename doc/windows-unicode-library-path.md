# Windows Unicode pure-library environment paths

The `xmind/unicode-library-path` branch starts from xMind's pinned SDK source
`b0da71f22e9591b6bd99883b21c50ad85dbe4bed`. It changes only Windows startup
handling of `XLANG3_PYTHON_LIB`, plus its regression test and evidence. Main was
fetched and inspected; unrelated main changes are not included in this narrow
xMind runtime fix.

A narrow CRT environment lookup followed by a narrow filesystem-path conversion
could lose a configured directory containing Unicode characters. The runtime
would then fall back to a different library or fail to find a required module.
Windows now uses the wide CRT lookup and native wide filesystem path. POSIX
behavior, import ordering and standard-library Python implementations are
unchanged. This does not add or execute CPython.

The native regression creates a real pure module beneath a directory named
`雪`, supplies that directory through the environment and imports the module
with the actual CLI. The preserved baseline fails with ModuleNotFoundError;
the candidate passes. Site startup is disabled only in this isolated native
import-root test. A separate actual xMind server/child test uses normal site
startup, a complete staged pure library in a Unicode directory and a relative
`--stdlib` argument. That test passes and verifies the actual library file and
SQLite approval environment binding. This is focused integration evidence,
not the complete xMind native gate or rendered UI acceptance.

Release CLI, interpreter, SDK stream-call and Unicode-library contracts pass
4/4. The candidate core/CLI were built from this branch; unchanged native
modules and Python facades were byte-checked against the preserved pinned SDK.
The original xlang3 checkout and its SDK were not modified.

The complete default performance gate passes all 11 cases, exit zero, using
21 paired samples, five warmups and the unchanged 10% threshold. The preserved
baseline runtime DLL SHA-256 is
`beec7e860b4a1e5088f28768275d2635988f14a494fe8973b66917c1791a4e47`;
the candidate runtime DLL is
`1ff2a8e71980a23773ea4d4ee863a14f59e7e56e8110c641a6b60e587a801cb3`.
Both sides used Release builds and the same inputs; no build ran during the
measurement. The gate controller itself ran under xlang3. The retained report
keeps all case data, with only the machine label redacted:
[performance report](windows-unicode-library-path-performance-20261018.json).

Local raw correctness, integration and performance logs are preserved under
`C:\xMindNativeValidation\xlang3-unicode-library-*20261018*` and
`C:\xMindNativeValidation\xmind-unicode-library-probe-20261018.*`.
