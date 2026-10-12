Static finding: the LoadException ownership repair does not change the timed
string-dict workload's execution metadata or register-transfer policy. The
measured C/D slowdown still requires compiled-code attribution; this finding
does not establish its machine-code cause or accept the candidate.

Pinned inputs

- Original body: `scratch/performance/string-dict-long-negative-control-20261008.py`, SHA256 `0adc297b8f203e80d8eaa6c9fda57ecc89b4d09bca19c4d887be125b43890bef`.
- Actual root-produced IR: `scratch/performance/string-dict-owner-repair-ir-20261008/string-dict-long-negative-control-20261008.ir.txt`, SHA256 `463b428b3f4adbbc9061a02849dec4de4a09aea2f5fd3c1975fb66ef12720724`.
- IR receipt: `doc/performance/data/string-dict-owner-repair-ir-20261008.json`, SHA256 `ee50a84d338104a2302010fe11d96c779c9a92380776413778fb7dadff0cc55b`; terminal exit0, unscored, all source/body/Release hashes unchanged.
- C frame header: `scratch/performance/exception-load-loop-owner-proposal-20261008-inputs/src/executor/xlang_vm/xlang_frame.h`, SHA256 `775349f214f8185f71d4e1885c056ea9db7743ae2f2a5b9cafd1c2c470d7011b`.
- D frame header: `src/executor/xlang_vm/xlang_frame.h`, SHA256 `c118a94e8243280fbd2e1587542fdd2f6320155498648f439c2847e29ca68fbf`.
- Shared call implementation: `src/executor/xlang_vm/ops/xlang_vm_ops_call.h`, SHA256 `fa72d225786d85527bbc50c3640792cebd41e8546f69b707c523ba13c6b05235`.
- Shared opcode rows: `src/executor/xlang_vm/xlang_vm_op_rows.h`, SHA256 `76b4c9d8116088e5e6487a24307b7b9fb90ed97322337ffaf39030fae349f08f`.
- Shared variable implementation: `src/executor/xlang_vm/ops/xlang_vm_ops_variables.h`, SHA256 `76b3c926a5f8e4b1e76a5946e57c4f6eb78db81d8f03f7acc023c3baa124e22e`.
- Shared loop implementation: `src/executor/xlang_vm/xlang_vm_loop.cpp`, SHA256 `2b95ce5dd9b188fb6a259fee7697942dd04d1b3eb57618230b8cd8e111653ce2`.
- Shared lowerer: `src/sema/lower.cpp`, SHA256 `ef8bf0b3185ee4d13180cd5be50e66a85ac255621983123570af55f35a4f9cbd`.

Concrete source/IR proof

All three actual IR functions (#0 `string_dict_get`, #1 `main`, #2 module)
contain zero LoadException instructions. The new boolean is therefore false
in every one. The shared module-load proof condition, producer classification,
read-before check, and incoming-edge admission reduce exactly to the C rules.
The additional exceptional-target guard is false for every module-load producer.

The timed #0 has 14 registers and 15 instructions. It also has no LoadModuleSlot
or identity instruction. In both headers its `need_module_load_loop_proof` is
false, so neither the linear snapshot nor new incoming-edge proof storage is
created. Its loop is IterNextLocal at 5, LoadLocalPair at 6, LoadConst at 7,
CallMethod at 8, LoadLocal at 9, InplaceAdd at 10, StoreLocal at 11, Jump 12→5.

At CallMethod 8, receiver r6 and key r7 are borrowed local projections; r8 is
the scalar default. Call result r9 is recreated at 8, noncarried, with final
read at 10. InplaceAdd result r10 is not classified as a recreated call/container
temporary and remains carried/MAX because StoreLocal 11 reads it. StoreLocal
therefore copies in both C and D. Iterator r4 and the other loop-read registers
retain the same conservative carried flags. No new register transfer reaches
this body.

The actual opcode is ordinary CallMethod, not CallLocalMethod. Its exact-string
dict.get success branch (`ops_call.h` approximately812–858) does not inspect
last-use/carried vectors and returns before instruction-cache preparation,
native argument adaptation, or push_frame. The mapping contains the exact
String key SELECT with the exact Int64 value 17 and invokes no Python hash or
equality callback. InplaceAdd and StoreLocal rows are unchanged.

`compute_register_last_use` returns on existing owner-matched metadata before
the new scan (`xlang_frame.h`941–947). A same-function frame reset skips the
method entirely (reset approximately458–470). The first untimed warmup enters
the same function used by all 21 timed samples; proof construction is cold.

Compiled-evidence boundary

The current loop object (`build-repro/main-verify-20261006/CMakeFiles/xlang3_runtime.dir/Release/src/executor/xlang_vm/xlang_vm_loop.cpp.obj`,
SHA256 `37ea8087001553b75070e298ad38d86091cc58c97cf5da4c4d1d66ac1d34fd8e`)
contains standalone compute_register_last_use, reset, and Interpreter.run_function
symbols. A standalone symbol does not rule out additional inlined instances.
A blind noinline annotation is therefore not justified yet.

VM reviewer owns the bounded C/D PE/export/.pdata/dispatch-byte comparison,
with actual CallMethod/IterNextLocal/InplaceAdd/StoreLocal loop anchors supplied.
No liveness counter run is needed to repeat this source proof. Any subsequent
structural experiment must retain the exact owning-exception dominance rules,
handler targets, nested-loop protection, and unchanged strict lifetime fixture.
It should be selected only from compiled-code evidence and measured against
the preserved complete D Release.

Only static file/hash/IR reads and this scratch note were performed here. No
Python, AST, engine/runtime, compiler, fixture, benchmark, or live source edit.
