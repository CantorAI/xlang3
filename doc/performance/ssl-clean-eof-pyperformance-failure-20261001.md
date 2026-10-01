# Native SSL clean EOF: pyperformance failure fix

The completed all-97 set/regex run failed `asyncio_tcp_ssl` with
`_ssl.SSLZeroReturnError: (6, 'TLS read: TLS/SSL connection has been closed')`.
The traceback reaches `asyncio.sslproto._do_read__copied`, then the Python
`SSLObject.read` wrapper and XLang3's native `_ssl` package. This is a runtime
compatibility failure, separate from async cases that reach the suite's
60-second cap. The websockets case instead failed because its dependency was
missing; those failure classes must remain distinct in the final status list.

CPython 3.14.7's
[`_ssl__SSLSocket_read_impl`](https://github.com/python/cpython/blob/v3.14.7/Modules/_ssl.c#L2507-L2556)
returns a zero-byte result when OpenSSL reports `SSL_ERROR_ZERO_RETURN` with
`SSL_RECEIVED_SHUTDOWN`. XLang3's generic native SSL I/O helper currently
converted that outcome to `SSLZeroReturnError` for reads as well as other I/O.

The change recognizes that exact clean-shutdown condition inside
the read operation and returns successful zero bytes. Handshake/write errors,
nonblocking retry conditions, timeout handling, callback errors, and abrupt
transport EOF retain their existing paths. The Python `ssl` and `asyncio`
modules remain Python; only XLang3's CPython-native-compatible `_ssl` module
changes. The code comment records why asyncio requires the EOF result.

The existing native SSL fixture now checks clean EOF through both read
overloads, an unchanged destination buffer, repeated EOF reads, and abrupt
EOF raising `SSLEOFError`. The same-source
[MemoryBIO reproduction](../../benchmarks/diagnostics/ssl_clean_eof.py)
passes under CPython and the rebuilt candidate. The saved control reproduces
`SSLZeroReturnError` on the first clean EOF read.

Raw correctness outputs: [CPython](data/ssl-clean-eof-cpython314-20261001.txt),
[control](data/ssl-clean-eof-xlang3-control-20261001.txt), and
[candidate](data/ssl-clean-eof-xlang3-candidate-20261001.txt).
The complete CTest suite passed **53/53**, including the expanded SSL and set
fixtures, in 45.56 seconds; see [full output](data/ssl-clean-eof-ctest-20261001.log).

The original native package is preserved at
`scratch/performance/validated-candidate-20261001/modules/xlang__ssl.x3pkg.dll`.
Its SHA-256 is
`2128B4B294C5C3C20C62EBC42C4EE6BEA0951B45088BE2C38821B525FE4EEBAE`.
The completed full suite used that original package. After it terminated,
only `xlang_ssl_native_package` was built. The executable and runtime DLL
retain their full-run hashes; the pending VM cache candidate is still unbuilt.

The rebuilt native package has SHA-256
`55F3D436D0580AE980C8C1C07E1360D4EC65B3338155FB09FCC7FFD7C8C6A8C5`.
All **11/11 cases passed** the complete fixed-baseline performance gate:
[JSON](data/ssl-clean-eof-regression-gate-20261001.json),
[console output](data/ssl-clean-eof-regression-gate-20261001.log).
This gate compares XLang3 builds and does not establish a CPython speed ratio.

The official fast rerun advanced through calibration and several measurement
workers without the original EOF exception, but hit the 300-second definition
cap. It produced no complete timing JSON. Keep the
[timeout output](data/asyncio-tcp-ssl-clean-eof-candidate-fast-20261001.log).
The unchanged official workload then completed in debug single-value mode at
**13.0 seconds**:
[JSON](data/asyncio-tcp-ssl-clean-eof-candidate-debug-20261001.json),
[log](data/asyncio-tcp-ssl-clean-eof-candidate-debug-20261001.log).
This transfers the benchmark's full 100 x 10 MiB payload and verifies its
original data-length assertion and connection shutdown. A single debug value
is correctness evidence, not a stable fast/rigorous performance score.

Status: correctness, complete fixed-baseline validation, and the official
workload in single-value mode passed. The 300-second fast timeout remains
recorded; no async speedup against CPython is claimed. Both official attempts
exposed the same existing Python package sources as the CPython reference.
