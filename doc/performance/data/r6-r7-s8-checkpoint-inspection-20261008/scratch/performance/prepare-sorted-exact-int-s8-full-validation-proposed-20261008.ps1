$ErrorActionPreference = 'Stop'
$root = 'D:\CantorAI\xlang3'
$baseRelative = 'scratch/performance/validate-pickle-published-frame-c6-full-20261008.py'
$outputRelative = 'scratch/performance/validate-sorted-exact-int-s8-full-20261008.py'
$utf8 = [System.Text.UTF8Encoding]::new($false)
function Sha([string]$path) { (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() }
if ((Sha (Join-Path $root $baseRelative)) -ne '04009fa27fee49ca20ba685818a7e122c774ea979931c4594501001ecf313400') { throw 'Frozen C6 full template changed' }
if (Test-Path -LiteralPath (Join-Path $root $outputRelative)) { throw 'Do not overwrite new full controller' }
$script:text = [System.IO.File]::ReadAllText((Join-Path $root $baseRelative)).Replace("`r`n", "`n")
function Replace-One([string]$old, [string]$new) {
  if (($script:text.Split(@($old), [System.StringSplitOptions]::None).Count - 1) -ne 1) { throw ('Expected exactly one transformation boundary: ' + $old) }
  $script:text = $script:text.Replace($old, $new)
}
Replace-One 'Fresh C6 published-frame ownership refresh validation and original pure-Python pickle benchmark.' 'Fresh S8 primitive sorted/R7 module binding checkpoint validation and original pure-Python pickle benchmark.'
Replace-One 'Require the exact current twelve-phase proof and an original-body diagnostic' 'Require the exact current fourteen-phase proof and an original-body diagnostic'
Replace-One 'meeting a root-supplied speedup threshold. All thread/CPP/full correctness/gate/pickle checks are fresh; C5 is historical provenance only.' 'with parity and stable hashes, plus a separately qualifying predeclared sorting-paired proof. All full correctness/gate/pickle checks are fresh; C5 is historical provenance only.'
Replace-One "BODY_CONTROLLER = ROOT / 'scratch/performance/run-pickle-published-frame-c6-original-body-and-sampling-20261008.py'`nBODY_CONTROLLER_SHA = 'd3d10bbf61edfaeaf92721952a18919ab1e8017e842051e933b4a0b893a8b699'`n" ''
Replace-One "SNAPSHOT_HEADER = 'tests/cpp/published_frame_snapshot_cases.h'" "S8_CASES = ('call_module_global_binding', 'sorted_exact_integer_keys')"
Replace-One "FOCUSED_PHASES = ['sorted7', 'iteration4', 'nested1', 'canonical10', 'fallback3', 'ownership2', 'owner2', 'namespace4', 'profile3', 'annotation2', 'cpp', 'threading']" "FOCUSED_PHASES = ['sorted7', 'iteration4', 'nested1', 'canonical10', 'fallback3', 'ownership2', 'owner2', 'namespace4', 'profile3', 'annotation2', 'threading', 'module_binding6', 'integer_sort7', 'cpp']"
Replace-One "    parser.add_argument('--body-controller', type=Path, default=BODY_CONTROLLER)`n    parser.add_argument('--body-controller-sha256', default=BODY_CONTROLLER_SHA)`n    parser.add_argument('--minimum-body-speedup', required=True, type=float,`n        help='Root-selected minimum historical C5-X/fresh C6-X unscored original-body ratio; must exceed 1x')" @'
    parser.add_argument('--body-controller', type=Path, required=True)
    parser.add_argument('--body-controller-sha256', required=True)
    parser.add_argument('--sorting-paired-receipt', type=Path, required=True)
    parser.add_argument('--sorting-paired-receipt-sha256', required=True)
'@
Replace-One "    parser.add_argument('--prefix', default='pickle-published-frame-c6-full-validation-20261008')" "    parser.add_argument('--prefix', default='sorted-exact-int-s8-full-validation-20261008')"
Replace-One "    assert math.isfinite(args.minimum_body_speedup) and args.minimum_body_speedup > 1`n" ''
Replace-One '    required_sources.add(SNAPSHOT_HEADER)' @'
    for name in S8_CASES:
        required_sources.update({'tests/fixtures/core/' + name + '.py', 'tests/fixtures/expected/' + name + '.out'})
'@
Replace-One "        'scope': 'Fresh C6 thread/CPP/full correctness validation after qualifying original pure-Python pickle body; exact current twelve-phase proof retained, all full phases fresh'," "        'scope': 'Fresh S8/R7 full correctness/default gate and original pure-Python pickle attempt; exact current fourteen-phase proof and body parity retained, all full phases fresh; sorting gain has its own predeclared paired prerequisite',"
Replace-One "'pickle_c6_full_timing_watch'" "'sorted_s8_full_timing_watch'"
Replace-One "body['focused_phases']==FOCUSED_PHASES" "len(body['focused_phases'])==len(FOCUSED_PHASES) and set(body['focused_phases'])==set(FOCUSED_PHASES)"
Replace-One "        assert body['unscored_c5_reference_comparison']['c5_over_c6']==speedup`n" ''
Replace-One "            'c6_x_seconds':actual_body['candidate-xlang3']['elapsed_seconds_diagnostic_only'],`n            'speedup_c5_over_c6':speedup,'minimum_speedup_required_by_root':args.minimum_body_speedup,`n            'scope':'Single original-body unpaired/unscored prerequisite only; no isolated-causality, official result or CP win claim'}" @'
            's8_x_seconds':actual_body['candidate-xlang3']['elapsed_seconds_diagnostic_only'],
            'historical_unpaired_c5_over_s8':speedup,
            'scope':'Fresh original-body parity and hash-stable correctness prerequisite only; historical ratio is unpaired/unscored and is not a performance acceptance threshold or CP win claim'}
'@
Replace-One "        assert speedup>=args.minimum_body_speedup, 'Candidate body does not meet root-selected useful-gain prerequisite'" '        verify_sorting_paired_receipt()'
Replace-One "'hash_exception_preservation', 'sqlite_statement_cache'):" "'hash_exception_preservation', 'sqlite_statement_cache', *S8_CASES):"
Replace-One '        required_cases.update(C5_CASES)' '        required_cases.update(C5_CASES); required_cases.update(S8_CASES)'
Replace-One "'sorted_key_scoped_entry', 'sorted_key_iteration_owner', 'sorted_key_nested_handled_context', *C5_CASES):" "'sorted_key_scoped_entry', 'sorted_key_iteration_owner', 'sorted_key_nested_handled_context', *C5_CASES, *S8_CASES):"
Replace-One "        assert cpp_text.count('#include `"published_frame_snapshot_cases.h`"')==1`n        assert cpp_text.count('xlang3::test::check_published_frame_same_owner_refresh(result);')==1`n" ''
Replace-One "{'core':396,'compatibility_sections':11,'expected_failures':3}" "{'core':398,'compatibility_sections':11,'expected_failures':3}"
$helperPath = Join-Path $root 'scratch/performance/sorted-exact-int-s8-sorting-receipt-check-proposed-20261008.inc'
if (-not (Test-Path -LiteralPath $helperPath)) { throw 'Freeze actual paired-schema helper before generation' }
$helper = [System.IO.File]::ReadAllText($helperPath).Replace("`r`n", "`n").TrimEnd()
Replace-One "    try:`n        save()`n        track(inventory_path, record['source_inventory_sha256'])" ($helper + "`n`n    try:`n        save()`n        track(inventory_path, record['source_inventory_sha256'])")
[System.IO.File]::WriteAllText((Join-Path $root $outputRelative), $script:text, $utf8)
[ordered]@{ output=$outputRelative; sha256=Sha (Join-Path $root $outputRelative); template_sha256=Sha (Join-Path $root $baseRelative) } | ConvertTo-Json
