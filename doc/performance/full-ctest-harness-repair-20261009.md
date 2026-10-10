# Complete CTest harness repair

The unchanged R7b Release engine passes all 55 registered CTests after two test
harness repairs. No VM, runtime, IR, IDE profile, or executable path changed.
This is correctness evidence, not a performance improvement.

Windows PowerShell 5 decodes native stdout with IBM437 when launched without a
console. A fresh `CREATE_NO_WINDOW` reproduction showed incorrect Unicode code
points from the JSON fixture despite its expected file being read as UTF-8.
The fixture runner now explicitly selects UTF-8 for native output and pipeline
input. Expected outputs and case coverage are unchanged.

The Visual Studio launch smoke test previously required the candidate executable
to occupy the developer IDE profile's default build directory. It now verifies
that the checked-in profiles agree on an XLang3 interpreter, then runs both the
adapter and debuggee with the executable explicitly supplied by CTest. Adapter
override consistency, environment, program, working directory, arguments and
the actual debugging checks remain enforced. The fixed candidate remains
`build-repro/main-verify-20261006/Release/xlang3.exe`.

The original full run passed 53 tests and failed these two tests. The repaired
run passes all 55, without filtering or a reference-run waiver. Both receipts
record terminal child cleanup and unchanged pinned engine/control/baseline files.
Earlier checkpoint receipts that covered nine selected CTests retain that scope;
they are not retrospectively full55 approvals.

- [Original full55 receipt](data/gc-r7b-full-ctest-control-20261009-receipt.json)
- [Original failures](data/gc-r7b-full-ctest-control-20261009.stdout.log)
- [Repaired full55 receipt](data/gc-r7b-full-ctest-harness-repair-20261009-receipt.json)
- [All 55 passing results](data/gc-r7b-full-ctest-harness-repair-20261009.stdout.log)
- [Complete command inventory](data/gc-r7b-full-ctest-harness-repair-20261009-inventory.stdout.log)

No speed or full pyperformance completion claim follows from these CTests.
