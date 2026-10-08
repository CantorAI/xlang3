The C-to-D long-dict regression has a verified compiled layout change, without added hot opcode instructions found in the compared dispatch function. This is evidence for a controlled cold-body separation experiment, not proof that layout caused the measured slowdown.

C is the failed correctness constructor58 control in `build-repro/controls/failed-cross-activation-constructor-before-ownership-repair-20261008`. Its runtime DLL SHA256 is `3d3a9733d81521de65cfabf32de9f1e75a679e791bf7bb516ee6877ae46fca75`. D is the correctness-validated, performance-held ownership59 control in `build-repro/controls/constructor-ownership-repair-held-20261008`. Its DLL SHA256 is `54fc3603b59987eb8cc333e05cb85cc18bd56830e46891d6889593c4b0baaa40`, and preservation manifest SHA256 is `7fc3a0e1f1344b1fdcc14cd0db91ad820dfb3e9a40a9043102aa418dd035505c`. Both DLLs lack a preserved PDB. The D disassembly was captured from the same bytes in live Release before the unrelated heapq rebuild; final byte analysis reads the immutable D control.

MSVC dumpbin 14.51.36256.0 exported-symbol and `.pdata` evidence:

| Function | C RVA range | D RVA range | C bytes | D bytes |
|---|---|---|---:|---:|
| Interpreter::run_function | 0x784450–0x79c9f0 | 0x784480–0x79ca20 | 99,744 | 99,744 |
| XlangVMFrame::compute_register_last_use | 0x771c30–0x77304d | 0x771c00–0x773079 | 5,149 | 5,241 |
| XlangVMFrame::reset | 0x7819a0–0x782ad9 | 0x7819d0–0x782b09 | 4,409 | 4,409 |
| XlangVMFrame::clear_for_pop | 0x771430–0x771a2f | 0x771400–0x7719ff | 1,535 | 1,535 |

The full run_function comparison has 19,862 instructions at exactly the same relative offsets and with the same instruction/operand structure through byte99,148. Only 64-bit external virtual-address operands were anonymized; internal targets retain their relative offsets. Stack setup remains `0x1160`. There are 5,407 raw byte differences in that prefix, so this result must not be called byte identity or complete external-call identity proof.

The final596bytes are149 switch-table RVAs. Dumpbin misinterprets those bytes as instructions; that accounts for every structural difference reported after offset99,148. Direct PE byte parsing proves that all149 values remain within run_function and have exactly identical destinations relative to its start. Every target shifts by48bytes along with the function. The entry changes from16 modulo64 in C to0 modulo64 in D. The cold metadata body grows92bytes. These observations identify code placement changes without demonstrating the CPU mechanism behind the 14.8% diagnostic regression.

Class source analysis `exception-load-owner-string-dict-semantic-review-20261008.md`, SHA256 `7201195e958a07410a0ae92cbb9a20d96ed4dfa5bbc68ff55e5b27f1c3888f59`, and root's actual frozen-body IR SHA256 `463b428b3f4adbbc9061a02849dec4de4a09aea2f5fd3c1975fb66ef12720724` establish that no executed function has LoadException. The timed loop's dict.get success returns before call binding/cache transfer; its ordinary liveness and StoreLocal behavior are unchanged. No IR work was repeated here.

The smallest justified experiment is to retain the atomic cached-owner check in the inline compute_register_last_use wrapper and move its exact unchanged slow body into the already compiled `src/executor/xlang_vm/xlang_interpreter.cpp`. Keep the LoadException ownership proof, handler bypass rejection, CAS publication/retry and all existing tests intact. The runtime already uses WINDOWS_EXPORT_ALL_SYMBOLS under ordinary /O2, and the current private compute method is exported; actual link checks remain required for the new standalone method. No CMake, opcode, class layout or dictionary algorithm change is needed.

Root should preserve this D control, build at the fixed path, pass the unchanged strict constructor fixture and C++ ownership proof, then use the unchanged long body against preserved D and accepted A before an expensive full validation. A failed timing trial should remain explicitly rejected. No performance benefit is predicted from the disassembly alone.

Retained artifacts are `constructor-owner-repair-{C,D}-exports-20261008.txt`, `constructor-owner-repair-{C,D}-pdata-20261008.txt`, `constructor-owner-repair-{C,D}-run-function-20261008.asm`, `constructor-owner-repair-run-function-structural-20261008.json`, and `constructor-owner-repair-run-function-switch-bytes-20261008.json`. Analysis used only native dumpbin and PowerShell/.NET file reads and scratch writes; no image loading, Python execution, runtime tests, build, benchmark or actual engine edits.
