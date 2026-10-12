# Native socket send-size trial — 2026-10-10

Status: source candidate prepared; Release build and timings are pending. This
is a measured hypothesis, not a claimed speedup.

## Finding

The pyperformance `bm_asyncio_tcp` workload creates one 10 MiB payload, writes
it 100 times, and drains after each write, for 1 GiB of loopback traffic per
iteration. The Python 3.14.7 selector transport calls `sock.send(data)` with
the pending buffer; after a partial write it schedules the remainder. XLang3's
native `_socket.socket.send` capped every OS call at 65,536 bytes, even when
the supplied buffer was larger. This can force additional send and selector
calls when the OS can accept more data at once.

CPython 3.14.7's native `sock_send_impl` passes the full buffer length to
`send()` and clamps only to `INT_MAX` on Windows. The candidate follows that
contract: Windows uses the same integer limit, while POSIX passes the remaining
buffer length. The OS can still report a partial write, and `sendall` continues
until all data has been accepted. No `asyncio` or other pure-Python library
implementation was changed.

Relevant references:

- CPython 3.14.7 [`socketmodule.c`](https://raw.githubusercontent.com/python/cpython/v3.14.7/Modules/socketmodule.c), `sock_send_impl`.
- CPython 3.14.7 `asyncio/selector_events.py`, `_sock_send` and
  `_SelectorSocketTransport.write`.
- XLang3 native implementation: [`socket_select_module.cpp`](../../src/runtime/modules/system/socket_select_module.cpp).

## Correctness coverage

[`socket_large_send.py`](../../tests/fixtures/core/socket_large_send.py) sends
a 1 MiB buffer while another thread drains the peer. It verifies that a
single `send()` result is a valid partial count and that exactly the reported
prefix arrives, then verifies `sendall()` transfers the complete buffer. It is
registered in `tests/run_fixtures.py` with expected output lines
`large-send-ok True` and `large-sendall-ok True`.

## Performance evidence and pending validation

The active pre-edit full run uses the fixed executable at
`build-repro/main-verify-20261006/Release/xlang3.exe` (SHA-256
`31A9C307298A41B8498CD5FD5E4E2EC3BBD19296975A7C63E587F5CD10DCAB3B`) and
runtime DLL (SHA-256
`C277CB7A2CAC0F7571626B0307361DD70DE6C775AA61099CDB4CA979CD584DEB`). Its
`asyncio_tcp` sample was 4.22 ± 1.26 seconds and pyperf marked it unstable;
`asyncio_tcp_ssl` exceeded the 300-second case cap. The log is
[`pyperformance-xlang3-current-candidate-full-fast-20261010-r1.log`](data/pyperformance-xlang3-current-candidate-full-fast-20261010-r1.log).
Those results come from the unchanged fixed binary and do not measure this
source candidate.

After the full baseline run finishes, build using the recorded VS command
[`vm-active-code-view-reproduction-build-command-20261009.cmd`](data/vm-active-code-view-reproduction-build-command-20261009.cmd),
run the socket fixture against XLang3 and CPython 3.14.7, and compare the
official `asyncio_tcp` and `asyncio_tcp_ssl` cases. Keep or revert the source
change based on those measurements. A full post-change suite is still needed
to establish whether this improves the suite-level result or changes failure
counts.
