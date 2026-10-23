# Embedding runtime lifetime serialization

The C API runtime create/destroy functions previously bypassed the execution
lock used by ordinary calls. A static-linked controlled probe holds that mutex
on another thread: its execution-guard control blocks, while both lifetime
operations complete. The unchanged concurrent isolation test also fails locally
on process7 with0xc0000374; hosted499 reports a segmentation fault in that test.
Raw failure and dump remain preserved; matching hosted/local causality is not
asserted from exception codes alone.

Both C API lifetime entry points now hold the existing execution guard across
new/delete, including member construction/destruction. Existing blocking thread
joins use execution suspension. No VM loop or optimization setting changes.
The native host guard contract now independently checks creation and destruction.

The five SDK contracts pass, including both new blocked=1 checks.100 separate
isolation invocations pass, each checking80 concurrent runtime lifetimes, with
source/binary file sets and hashes unchanged. Three ctypes compatibility cases
pass. The complete default11-case performance gate passes21 pairs/5 warmups
with the original10% threshold against the preserved fixed Release baseline;
see [raw report](data/runtime-lifetime-lock-20261023.json). The prior499 Release
was preserved separately before editing. No CPython executed.

Candidate engine/test patch SHA256: 3602ccefd6f2c703feb1069f1e0d516090140331aaa3eebee437aaf11d007a31

This is SDK qualification; fresh xMind complete/native hosted qualification and
packaging remain separate. Installed xMind is unchanged.
