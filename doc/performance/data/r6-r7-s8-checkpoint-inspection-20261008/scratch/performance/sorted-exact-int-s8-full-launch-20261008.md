The controller is held until root has terminal, hash-stable S8 body parity and a qualifying terminal sorting-paired receipt. No body speedup threshold exists. The sorting rule was declared before results: seven alternating C5/S8 pairs, median plain-sort ratio strictly above 1.10 and bootstrap lower 95% bound strictly above 1.0. Every child and all five diagnostic rows remain retained.

Frozen controller: `scratch/performance/validate-sorted-exact-int-s8-full-20261008.py`, SHA256 `997d0e694f5c04aa2459bfbaca36065054c53d1661f810e37c5f7dd7b50813f3`. Proof: `sorted-exact-int-s8-full-controller-provenance-20261008.json`, SHA256 `081ee3e95a339a6756eea3185b37b2a726bef3c29db132987ecfca287e467be2`. No AST or runtime was executed by the author.

Root supplies actual terminal body receipt/controller paths and hashes and the actual paired receipt hash; those future hashes are deliberately unspecified. After root's static/AST check and an idle host, the launch shape from `D:/CantorAI/xlang3` is:

```powershell
& C:/Python/Python314/python.exe -I scratch/performance/validate-sorted-exact-int-s8-full-20261008.py `
  --source-inventory doc/performance/data/sorted-exact-int-s8-registered-source-20261008.json `
  --source-inventory-sha256 f20c304e32b0874c929d22dc72018cbd6d5fd9d124844c1d66d8edcc6a231556 `
  --focused-receipt doc/performance/data/sorted-exact-int-s8-registered-focused-20261008.json `
  --focused-receipt-sha256 9fbe5bc1eb111876175ed630d0140ac6f0b9d1eb4292ac956778c81cd2fe4d5f `
  --body-receipt $s8BodyReceipt --body-receipt-sha256 $s8BodyReceiptSha `
  --body-controller $s8BodyController --body-controller-sha256 $s8BodyControllerSha `
  --sorting-paired-receipt doc/performance/data/sorted-exact-int-s8-paired-20261008.json `
  --sorting-paired-receipt-sha256 $s8PairedReceiptSha `
  --build-log $s8BuildLog --build-log-sha256 $s8BuildLogSha `
  --prefix sorted-exact-int-s8-full-validation-20261008
```

All parameter paths must identify real retained files. The controller refuses an existing prefix. The build arguments are optional as a pair but should preserve root's actual raw build output; no build is launched. If root changes the final source inventory, use its actual path/SHA and new matching focus/body/paired proof; do not retain stale source maps.

Dependencies are the frozen C6 full template `04009fa2…`, original pure-Python pickle child `5295c899…`, sorting producer `5fc95048…`, unchanged callback diagnostic `f7613659…`, complete immutable C5 control manifest `85910ee3…`, the old reference receipt and process watcher already pinned by that template, and the saved exact CPython 3.14.7 benchmark definition/reference. Root must keep the source110 tree and complete current178 Release files unchanged for this run.

Every full phase is fresh: 398 core fixtures, 11 compatibility sections, three expected failures, nine configured CTest cases and two manual SQLite API cases. The fixed accepted baseline is `build-repro/Release/xlang3.exe`; the gate stays 11 cases, 21 repeats, five warmups and 0.10 threshold. The official attempt remains the original `pickle_pure_python` definition, protocol5, 20 values and 300-second case cap. Any timeout, incomplete score or hash/process-overlap failure remains visible and cannot be promoted to complete validation. This controller does not run full97, recapture CP references, stage files or claim a CPython win.
