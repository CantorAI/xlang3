# Held C6 pickle trial controllers

Use these only after root applies/builds C6 and produces a successful, hash-stable twelve-phase focused receipt. The final source inventory, receipt and hashes are caller inputs. No C6 source or binary hashes are guessed.

The focused phases are `sorted7`, `iteration4`, `nested1`, `canonical10`, `fallback3`, `ownership2`, `owner2`, `namespace4`, `profile3`, `annotation2`, `cpp`, and `threading`. The CPP target must include and call the new public snapshot proof once. `threading` runs the unchanged `threading_runtime_edges.py` against its exact existing golden output.

Root sets `$FinalInventoryPath`, `$FinalInventorySha`, `$FocusedReceiptPath`, and `$FocusedReceiptSha` from the actual C6 build and targeted result. Run one unprofiled original body per runtime, CPython 3.14.7 followed by the fixed current XLang3 executable:

```powershell
& C:/Python/Python314/python.exe -I scratch/performance/run-pickle-published-frame-c6-original-body-and-sampling-20261008.py --mode body --source-inventory $FinalInventoryPath --source-inventory-sha256 $FinalInventorySha --focused-receipt $FocusedReceiptPath --focused-receipt-sha256 $FocusedReceiptSha --prefix pickle-published-frame-c6-body-20261008
```

Body controller SHA256: `d3d10bbf61edfaeaf92721952a18919ab1e8017e842051e933b4a0b893a8b699`.

The original child is unchanged at `5295c899d95d023ca2fb3b511e9bf78b58cb49e4a256770c65adf0d614b14109`: 41 loops, three original objects, 20 dumps per object per loop, protocol 5, 2,460 dumps. Byte signatures and pure-Python roundtrips must match. This is a single unpaired, unscored diagnostic, with a 300-second cap and separate raw streams. Optional `sample` mode is a separate root decision and requires the exact same-candidate successful body receipt; it never launches automatically.

C5 is immutable historical evidence: complete 178-file control plus 106-source snapshot, terminal correctness and gate pass with failed pprint official, original pickle body CP 0.2121355 seconds / X 4.2574951 seconds, and the 190-location native sample. C6 uses no inherited C5 correctness phase. The C5 native sample is not elapsed-time comparison evidence.

If the targeted result and body justify further work, root chooses a finite `$MinimumUsefulGain` greater than 1, and sets `$BodyReceiptPath`, `$BodyReceiptSha`, `$BuildLogPath`, and `$BuildLogSha` from the actual results. Full validation is fresh:

```powershell
& C:/Python/Python314/python.exe -I scratch/performance/validate-pickle-published-frame-c6-full-20261008.py --source-inventory $FinalInventoryPath --source-inventory-sha256 $FinalInventorySha --focused-receipt $FocusedReceiptPath --focused-receipt-sha256 $FocusedReceiptSha --body-receipt $BodyReceiptPath --body-receipt-sha256 $BodyReceiptSha --minimum-body-speedup $MinimumUsefulGain --build-log $BuildLogPath --build-log-sha256 $BuildLogSha --prefix pickle-published-frame-c6-full-validation-20261008
```

Full controller SHA256: `04009fa27fee49ca20ba685818a7e122c774ea979931c4594501001ecf313400`.

This retains 396 core fixtures, 11 compatibility sections, three expected failures, all nine configured CTest cases, both manual SQLite APIs, and the unchanged fixed 11-case gate with 21 repeats, five warmups and a 0.10 threshold against `build-repro/Release`. It adds a fresh thread-inspection check and runs the original official `pickle_pure_python` definition with its exact `--pure-python pickle` options and default protocol 5. One exact subtest must have 20 complete values; the definition cap remains 300 seconds. The saved CPython 3.14.7 Oct7 row also has 20 values. Its comparison is explicitly unpaired, with no whole-suite completion or CPython-win claim.

No controller, AST check, interpreter, benchmark, build, source edit or staging operation was executed by this subagent. Old controllers and receipts remain unchanged. Provenance: `pickle-published-frame-c6-controller-provenance-20261008.json`, SHA256 `5e3eb4636593579e5679e48bb565ba7dd4e4103db4f6a04be9ba6dc40bcfa959`.
