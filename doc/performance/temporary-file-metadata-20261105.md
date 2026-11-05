# Writable FileIO name metadata and multipart spooling

Linux `tempfile.TemporaryFile()` assigns an integer descriptor to `raw.name`.
XLang3 accepted only string names and rejected that assignment, so multipart
uploads crossing Starlette's 1 MiB spooling boundary failed before admission.
The same issue reproduced in the preserved native baseline.

`FileIO.name` now stores arbitrary Python metadata in the existing attribute
storage. Changing that metadata does not retarget the underlying descriptor or
filesystem path. Reinitializing FileIO replaces its name and preserves unrelated
instance attributes. The pure-Python tempfile and multipart implementations are
unchanged; uploads still spill to disk rather than raising the memory threshold.

The new core fixture checks integer/object/string names, descriptor round trips,
reinitialization, both 1 KiB and 1 MiB spooling thresholds, closed descriptors and
temporary-file cleanup. Its expected output was checked with CPython 3.14. The
FastAPI upload contract now also sends a 1.44 MB body through TestClient.

Windows passed 13 native I/O/GC fixtures and the native FastAPI multipart
contract. Linux passed the new fixture and native async multipart round trips
with no CPython library loaded. Seven of nine selected Linux fixtures matched
their goldens. `file_io_compat` (newline/tell values) and `filesystem_io_edges`
(`fstat(-1)` errno) differ from their Windows-oriented goldens identically in
the baseline and candidate; there are no new fixture differences. No existing
C exports were removed.

Both Release performance gates passed the complete 11-case default suite with
21 order-balanced pairs, five warmups and the unchanged 10% threshold:

- [Windows qualified rerun](data/temporary-file-metadata-windows-20261105.json)
- [Linux XPS qualification](data/temporary-file-metadata-linux-20261105.json)

The first Windows run was inconclusive for `deepcopy_memo` due to a wide timing
interval. Its [original result](data/temporary-file-metadata-windows-inconclusive-20261105.json)
is retained. The qualified rerun used the same preserved baseline, candidate,
suite and P-core affinity, with AboveNormal priority applied equally to the
finite baseline/candidate benchmark children. Existing service priorities were
unchanged. Linux ran separately on XPS, pinned to CPU 4 with one CPU quota,
1 GiB memory, 16 tasks and a 30-minute service limit; it exited successfully.
No benchmark was run on the Azure serving host.

These results qualify this runtime change. They do not establish full fleet
rollout, maximum cluster throughput, or end-to-end long-ASR acceptance.
