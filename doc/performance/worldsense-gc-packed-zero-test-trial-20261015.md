# WorldSense GC packed zero-test trial — 2026-10-15

This is an unbuilt, unmeasured source trial. No installation or acceptance is
permitted on the strength of this hypothesis. Preserve the fixed accepted
workspace20827bca baseline, all11 cases/21 pairs/five warmups/10% tolerance,
and the original affected GC definitions.

The preceding indexed module-root trial, workspacef5b71b39/XLanga46f2f3e,
passes Windows downloaded448 fixtures/13 contracts and native Windows, Intel
Mac and Pi5b gates. Its complete fixed performance gate remains inconclusive:
GC first1.09359x with95% interval[1.08536,1.10574], confirmation1.08401x
with[1.07353,1.09193]. The unchanged gate conservatively retains inconclusive.
Original pyperformance1.14.0 fast runs (20 values each) establish no win over
the preceding181 control: create_gc_cycles3.7058 versus3.6753ms;
gc_traversal3.5655 versus3.4979ms. Both drivers exit0; stability warnings remain.

An offline audit of the preserved181 profile checked the matching DLL/PDB and
PE unwind ranges. Public PDB names have no function size; nearest-symbol labels
are insufficient for phase attribution. Of16396 runtime samples,4397 fell in
the private packed sequence scanner, not the nearby public int64 vector
constructor. Disassembly at RVA0x1be40a–0x1be50f identifies the actual loads,
four full-Value XORs, three ORs, PCMPEQB/PMOVMSKB/CMP and bounded loop.
The module-root predicate's3759 samples are independently within its actual
PE function range. The new audit changes attribution without running another
workload. No vector-allocation optimization follows from the misleading name.
[Sanitized audit](data/worldsense-gc-packed-loop-profile-audit-20261015.json)
records exact captured image/PDB identities and instruction ranges.

The hypothesis is narrower instruction selection in the measured packed loop:
PTEST can check the combined128 difference bits without materializing a byte
mask. Windows MSVC x64 selects a separately instantiated loop once per sequence
after cached CPUID leaf1 ECX bit19. Older Windows CPUs keep SSE2; other x86-64
compilers retain SSE2 and ARM retains the scalar path. No global compiler ISA
requirement or per-VM-instruction check is added. The full16-byte comparison,
four-element bounds, borrowed flags, multiplicity, scalar tails and graph work
are identical. The benchmark host's .NET intrinsic capability query reports
SSE4.1 support on its four-logical-processor CantorAI003 CPU.

Before editing, the generic cycle R2–R7b trials, MRO edge trial and prior
module-root trial were read, including the existing packed SSE2 source. Searches
of preserved performance notes/patches found no PTEST/SSE4.1 GC trial. This
does not retry rejected per-edge hashing, graph layout or ownership removal.

Existing native cycle tests now include lengths around four-, eight-, sixteen-,
thirty-two- and sixty-four-element boundaries with scalar/borrowed mismatches,
external roots and eventual reclamation. Required validation is GitHub native
correctness, the complete fixed gate, original affected benchmarks, and
inspection that the Windows binary actually selects guarded PTEST. Neither
these pending tests nor older hardware evidence accepts this trial.
