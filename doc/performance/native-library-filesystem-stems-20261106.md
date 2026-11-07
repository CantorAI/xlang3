# Native library filesystem stems, November 6, 2026

An SDK import of an extensionless native library path failed when its directory
contained a dot. Runtime import treated the directory prefix as a Python parent
module. The failure reproduced on the fixed accepted Windows Release runtime,
the serving runtime, and the pre-change candidate; this was an existing defect.

Explicit filesystem references now enter native library lookup before dotted
Python module resolution. Relative stems resolve against the caller's working
directory, and absolute stems use the specified directory. They do not depend
on Python import roots or native namespace-prefix fallbacks. Ordinary dotted
Python module names retain their existing path. The change adds no background
polling or per-instruction work.

The C++ SDK regression checks absolute stems, a deterministic dotted-directory
spelling, and relative stems. Repeated imports initialize each package only
once per runtime. The existing explicit-suffix and Windows Unicode working
directory checks remain intact.

Windows and Linux x86-64 Release builds each passed five native CTest cases.
Windows passed five CLI fixtures; Linux passed six, including statvfs. All 90
public C exports were preserved on both platforms. For the Linux sys metadata
fixture, only the platform-specific printed sys._vpath representation was
normalized after verifying identical output from the preserved native runtime.
The fixture and its own checks were unchanged. Separate finite Windows native
processes confirmed that each executable loaded its own intended runtime DLL,
with no CPython DLLs mapped.

Both complete default performance gates passed: 11 cases, 21 order-balanced
paired measurements, five warmups, and the unchanged 10% threshold. The fixed
accepted baseline was not replaced. No build overlapped its host's benchmark;
Linux CPU task admission was held during measurement and restored afterward.
The table shows candidate/baseline median ratios. Windows compares cumulative
source and compiler history against its fixed accepted binary; its larger
speed differences are not attributed to this path lookup fix. These gates do
not establish a new baseline or resolve historical performance differences.

| Case | Windows | Linux |
| --- | ---: | ---: |
| local_slots | 0.582 | 1.005 |
| scalar_arithmetic | 0.447 | 1.014 |
| range_for | 0.107 | 1.009 |
| function_calls | 0.087 | 0.935 |
| class_construct | 0.469 | 1.012 |
| list_append | 0.040 | 1.001 |
| property_access | 0.084 | 0.906 |
| deepcopy_memo | 0.988 | 0.985 |
| json_dumps | 0.869 | 0.968 |
| gc_traversal | 0.402 | 0.990 |
| subparsers | 1.053 | 0.826 |

The complete paired samples, executable/runtime identities, native mapping
checks and CLI results are in
[native-library-filesystem-stems-20261106.json](native-library-filesystem-stems-20261106.json).

This qualification does not claim deployment, real Cantor worker execution,
or qualification of other platforms. Those remain separate rollout checks.
