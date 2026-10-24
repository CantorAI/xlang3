$ErrorActionPreference = 'Stop'
$repoRoot = 'D:/CantorAI/xlang3'
Set-Location -LiteralPath $repoRoot
$base = 'scratch/performance/validate-sorted-exact-int-s8-full-20261008.py'
$target = 'scratch/performance/validate-frame-locals-retirement-r4-full-r2-proposed-20261008.py'
$baseHash = '997d0e694f5c04aa2459bfbaca36065054c53d1661f810e37c5f7dd7b50813f3'
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $base).Hash.ToLowerInvariant() -ne $baseHash) { throw 'Accepted full validation parent changed' }
if (Test-Path -LiteralPath $target) { throw 'Refusing frozen controller overwrite' }
$text = [IO.File]::ReadAllText((Join-Path $repoRoot $base)).Replace("`r`n","`n")
function Replace-Once([string]$old,[string]$new) {
    if (($script:text.Split(@($old),[StringSplitOptions]::None)).Count -ne 2) { throw ('Nonunique boundary: ' + $old) }
    $script:text = $script:text.Replace($old,$new)
}
function Replace-Range([string]$startText,[string]$endText,[string]$replacement) {
    $start = $script:text.IndexOf($startText,[StringComparison]::Ordinal)
    $end = $script:text.IndexOf($endText,$start,[StringComparison]::Ordinal)
    if ($start -lt 0 -or $end -le $start) { throw ('Missing range: ' + $startText) }
    $fragment = $replacement.Replace("`r`n","`n")
    if ($fragment.Length -gt 0 -and -not $fragment.EndsWith("`n")) { $fragment += "`n" }
    $script:text = $script:text.Substring(0,$start) + $fragment + $script:text.Substring($end)
}
$header = @'
"""Fresh frame-locals retirement full correctness, fixed gate and original pure-Python pickle.

Root-only exact CPython3.14.7 controller. Require caller-pinned current111 source,
fresh ten-phase focused proof and the actual successful build command/raw log.
The passed current focused proof is prerequisite only; all cross-suite phases
are fresh. Historical S8 is reference/preservation evidence only. No old S8
correctness phase is reused, and no build or CP reference recapture is launched.
"""
'@
Replace-Range '"""Fresh S8' 'import argparse' ($header+"`n")
$constants = @'
PARENT_CONTROLLER = ROOT / 'scratch/performance/validate-sorted-exact-int-s8-full-20261008.py'
PARENT_CONTROLLER_SHA = '997d0e694f5c04aa2459bfbaca36065054c53d1661f810e37c5f7dd7b50813f3'
S8_VALIDATION = DATA / 'sorted-exact-int-s8-full-validation-20261008.json'
S8_VALIDATION_SHA = 'd4b028b1255f23b11e664bd9e6778b808a7d160d3628e17f39710faa1900d022'
APPLICATION = DATA / 'frame-locals-retirement-s8-application-20261008-applied-source.json'
APPLICATION_SHA = 'dc563e2b363dc3992a2c8b323e0f59e953749a715bb4ec97264ef1fa846ab153'
PROOF = ROOT / 'scratch/performance/frame-locals-retirement-r4-proposed-20261008-provenance.json'
PROOF_SHA = '24b1c58347f3bea0c4bc40d5e450efb04c8ff614f4c0b482cc12511380cf8e76'
PRESERVED = ROOT / 'build-repro/controls/frame-locals-retirement-s8-application-20261008'
PRESERVED_SHA = '29841491e3a6e934db9ef19dfdd01ef3ef71ab14e2cc3333b80213d793ab9e26'
BUILD_TERMINAL = DATA / 'frame-locals-retirement-r4-build-terminal-20261008.json'
BUILD_TERMINAL_SHA = 'df8ba2f7b324b6b29173e7622f47fbaa6a8c9ea86560e24db2557b1e8c93ae0a'
FRAME_REFERENCES = (
    ('runtime-frame-context-cpython3147-s8-correctness-r2-20261008.json',
     '159f7294d3ff4e1395722145e3bc8ef72a6ed47834989798cd588af0b5af7d41', 1),
    ('frame-locals-retained-cpython3147-s8-reference-20261008.json',
     '71a0288e97ffb357de8e447a7a9fca38450fb173201b708988d74341d7150751', 2))
FRAME_FIXTURES = (
    ('framecontext4', 'scratch/performance/runtime-frame-context-coalesced-fixture-proposed-20261008.py',
     'scratch/performance/runtime-frame-context-coalesced-expected-proposed-20261008.out',
     'fd942dfb3981c01b6464f623ebb1b37640772c532b18ad7381abebf2d9e5ffc7',
     '97d5a6c2764deb48e4f217f09d148f13b4f8332d88b6f930f14ba12ede9e065d'),
    ('retained1', 'scratch/performance/frame-locals-retained-mapping-fixture-proposed-20261008.py',
     'scratch/performance/frame-locals-retained-mapping-expected-proposed-20261008.out',
     '9cf97667d8ef24562c857f651bfd8d5489281e6fd3887f888995a63f256839df',
     '3498e5609c21461d63e16be4273c358cac309f47c59918e0632b95c2c7914150'),
    ('extra1', 'scratch/performance/frame-locals-retained-extra-fixture-proposed-20261008.py',
     'scratch/performance/frame-locals-retained-extra-expected-proposed-20261008.out',
     'ec47ae1e050c1c367125466834185d5cd9edb0b002ad9b03bac1213ae7686a88',
     'bcc83f86c18cc6060a085ea6736038efcb31a42bf2830f71fdb4f64e6eec3e71'))
'@
Replace-Range 'BODY_CHILD = ROOT /' 'THREAD_SOURCE = ROOT /' ($constants+"`n")
Replace-Once "FOCUSED_PHASES = ['sorted7', 'iteration4', 'nested1', 'canonical10', 'fallback3', 'ownership2', 'owner2', 'namespace4', 'profile3', 'annotation2', 'threading', 'module_binding6', 'integer_sort7', 'cpp']" "FOCUSED_PHASES = ['cpp', 'framecontext4', 'retained1', 'extra1', 'debug_frames', 'profile3', 'threading', 'monitoring', 'sorted7', 'nested1']"
Replace-Range "    parser.add_argument('--body-receipt'" "    parser.add_argument('--identity-reference'" @'
    parser.add_argument('--focused-controller', required=True, type=Path)
    parser.add_argument('--focused-controller-sha256', required=True)
    parser.add_argument('--build-log', type=Path, required=True)
    parser.add_argument('--build-log-sha256', required=True)
    parser.add_argument('--prefix', required=True)
'@
Replace-Once 'assert sys.version_info[:3] == (3, 14, 7) and Path(sys.executable).resolve() == CPYTHON.resolve()' 'assert sys.implementation.name == "cpython" and sys.version_info[:3] == (3, 14, 7) and Path(sys.executable).resolve() == CPYTHON.resolve()'
Replace-Once 'assert sys.flags.optimize == 0 and bool(args.build_log) == bool(args.build_log_sha256)' 'assert sys.flags.optimize == 0 and ROOT == Path("D:/CantorAI/xlang3").resolve() and Path.cwd().resolve() == ROOT'
Replace-Once "    assert digest(inventory_path) == args.source_inventory_sha256" "    assert digest(inventory_path) == args.source_inventory_sha256 == APPLICATION_SHA and len(sources) == 111"
Replace-Once "    assert required_sources <= sources.keys()," "    required_sources.add('tests/cpp/frame_locals_retirement_cases.h')`n    assert required_sources <= sources.keys(),"
Replace-Once "'scope': 'Fresh S8/R7 full correctness/default gate and original pure-Python pickle attempt; exact current fourteen-phase proof and body parity retained, all full phases fresh; sorting gain has its own predeclared paired prerequisite'," "'scope': 'Fresh frame-retirement full correctness/default fixed11 gate/original pure-Python pickle20; exact same-candidate targeted proof is prerequisite only, every full phase fresh; no old correctness reuse or speedup prerequisite',"
Replace-Range '    def verify_sorting_paired_receipt():' "    try:`n        save()" ''
$start = $text.IndexOf('    def verify_sorting_paired_receipt():',[StringComparison]::Ordinal)
if ($start -ge 0) { throw 'Sorting prerequisite was not removed' }
# The removed helper contains no earlier top-level four-space try boundary.
Replace-Once "spec_from_file_location('sorted_s8_full_timing_watch', WATCH)" "spec_from_file_location('frame_retirement_full_timing_watch', WATCH)"
Replace-Range '        if args.build_log:' '        for name, sha in sources.items():' @'
        track(PARENT_CONTROLLER, PARENT_CONTROLLER_SHA)
        track(S8_VALIDATION, S8_VALIDATION_SHA)
        parent_validation = json.loads(S8_VALIDATION.read_bytes())
        assert parent_validation['terminal'] and parent_validation['status'] == 'validated'
        assert parent_validation['full_validated'] and parent_validation['hashes_unchanged'] and parent_validation['release_tree_unchanged']
        assert parent_validation['terminal_record']['controller_sha256'] == PARENT_CONTROLLER_SHA
        assert parent_validation['fixture_counts'] == dict(core=398, compatibility_sections=11, expected_failures=3)
        track(PROOF, PROOF_SHA); proof = json.loads(PROOF.read_bytes())
        track(BUILD_TERMINAL, BUILD_TERMINAL_SHA); build = json.loads(BUILD_TERMINAL.read_bytes())
        assert build['terminal'] and build['status'] == 'build_completed' and build['exit_code'] == 0
        assert build['application_receipt_sha256'] == APPLICATION_SHA and (ROOT / build['application_receipt']).resolve() == inventory_path
        build_path = args.build_log.resolve(strict=True)
        assert build_path == (ROOT / build['build_log']).resolve()
        assert args.build_log_sha256 == build['build_log_sha256']
        track(build_path, args.build_log_sha256); track(ROOT / build['wrapper'], build['wrapper_sha256'])
        assert build['command'] == ['cmd.exe', '/d', '/c', 'scratch\\performance\\build-frame-locals-retirement-r4-20261008.cmd']
        assert inventory['terminal'] and inventory['status'] == 'applied_unbuilt_unvalidated_frame_locals_retirement_correctness'
        assert inventory['proposal_sha256'] == PROOF_SHA and inventory['source_count'] == 111
        assert not inventory['accepted_gate_baseline_changed'] and inventory['baseline_before'] == inventory['baseline_after']
        assert sources == dict(parent_validation['source_sha256'], **proof['candidate_source_sha256'])
        assert inventory['owned_targets'] == proof['candidate_file_list'] and len(proof['candidate_source_sha256']) == 3
        for path, sha in proof['candidate_source_sha256'].items(): track(ROOT / proof['candidate_root'] / path, sha)
        for path, sha in parent_validation['source_sha256'].items(): track(ROOT / proof['raw_input_root'] / path, sha)
        manifest_path = PRESERVED / 'preserved-release-provenance.json'
        track(manifest_path, PRESERVED_SHA); preserved = json.loads(manifest_path.read_bytes())
        assert inventory['preserved_manifest_sha256'] == PRESERVED_SHA and Path(inventory['preserved_parent']).resolve() == PRESERVED.resolve()
        assert preserved['source_snapshot_sha256'] == parent_validation['source_sha256'] and preserved['source_count'] == 110
        assert preserved['file_count'] == len(preserved['files_sha256']) == 178
        assert preserved['files_sha256'] == {Path(path).relative_to(CANDIDATE.parent.relative_to(ROOT)).as_posix(): sha for path, sha in parent_validation['binaries_sha256'].items()}
        assert preserved['baseline_sha256'] == inventory['baseline_after'] and len(preserved['baseline_sha256']) == 177
        baseline = {path.relative_to(BASELINE.parent).as_posix(): digest(path) for path in BASELINE.parent.rglob('*') if path.is_file()}
        assert baseline == preserved['baseline_sha256']
        record['baseline_sha256'] = baseline
        for path, sha in baseline.items(): track(BASELINE.parent / path, sha)
        for directory, values in ((PRESERVED, preserved['files_sha256']), (PRESERVED / 'source-snapshot', preserved['source_snapshot_sha256']),
            (PRESERVED / 'unrelated-dirty-snapshot', preserved['unrelated_dirty_snapshot_sha256'])):
            for path, sha in values.items(): track(directory / path, sha)
        preserved_tree_before = {path.relative_to(PRESERVED).as_posix(): digest(path) for path in PRESERVED.rglob('*') if path.is_file()}
        record['preserved_build_log'] = dict(path=str(build_path), sha256=args.build_log_sha256, exit_code=0,
            terminal_record=BUILD_TERMINAL.name, terminal_record_sha256=BUILD_TERMINAL_SHA, actual_command=build['command'], build_launched=False)
        record['historical_s8_reference'] = dict(record=S8_VALIDATION.name, sha256=S8_VALIDATION_SHA, correctness_reused=False)
'@
Replace-Once '        focused_path = args.focused_receipt.resolve(strict=True)' "        focused_controller = args.focused_controller.resolve(strict=True)`n        assert focused_controller == (ROOT / 'scratch/performance/check-frame-locals-retirement-r4-focused-proposed-20261008.py').resolve()`n        assert args.focused_controller_sha256 == 'f6c352a403312cd97b9370e04390394ae9745df17b36f078fd169d7e961abc42'`n        track(focused_controller, args.focused_controller_sha256)`n        focused_path = args.focused_receipt.resolve(strict=True)"
Replace-Once "        assert focused['source_sha256'] == sources and focused['source_inventory_sha256'] == digest(inventory_path)" "        assert focused['controller_sha256'] == args.focused_controller_sha256 and focused['application_receipt_sha256'] == APPLICATION_SHA`n        assert focused['source_sha256'] == sources and focused['source_inventory_sha256'] == digest(inventory_path)`n        assert focused['baseline_sha256'] == baseline and focused['root_recorded_build_exit_code'] == 0`n        assert focused['build_receipt_sha256'] == BUILD_TERMINAL_SHA and focused['build_wrapper_sha256'] == build['wrapper_sha256']`n        assert focused['build_log_sha256'] == args.build_log_sha256 and Path(focused['build_log']).resolve() == build_path`n        assert focused['build_argv'] == build['command']`n        assert focused['build_argv_sha256'] == hashlib.sha256(json.dumps(build['command'], separators=(',', ':')).encode('utf-8')).hexdigest()"
Replace-Range '        body_path = args.body_receipt.resolve(strict=True)' '        track(BASELINE,' @'
        assert focused['hashes_before'] == focused['hashes_after']
        for path, sha in focused['hashes_before'].items(): track(Path(path), sha)
        track(THREAD_SOURCE, THREAD_SOURCE_SHA); track(THREAD_EXPECTED, THREAD_EXPECTED_SHA)
        references = []
        for filename, sha, cp_count in FRAME_REFERENCES:
            path = DATA / filename; track(path, sha); reference = json.loads(path.read_bytes())
            assert reference['terminal'] and reference['hashes_unchanged']
            cp_rows = [row for row in reference['phases'] if row['runtime'] == 'cpython3147']
            assert len(cp_rows) == cp_count and all(row['passed'] and row['exit_code'] == 0 and not row['timeout'] and row['output_matches_expected'] for row in cp_rows)
            for row in reference['phases']:
                for stream in ('stdout', 'stderr'): track(DATA / row[stream + '_log'], row[stream + '_sha256'])
            references.append(reference)
        assert references[0]['source_sha256'] == FRAME_FIXTURES[0][3] and references[0]['expected_sha256'] == FRAME_FIXTURES[0][4]
        for row, fixture in zip(references[1]['fixtures'], FRAME_FIXTURES[1:]):
            assert row['source_sha256'] == fixture[3] and row['expected_sha256'] == fixture[4]
        for _, source, expected, source_sha, expected_sha in FRAME_FIXTURES:
            track(ROOT / source, source_sha); track(ROOT / expected, expected_sha)
'@
Replace-Once '        track(CP_PROVENANCE)' "        track(CP_PROVENANCE, parent_validation['hashes_before'][str(CP_PROVENANCE.resolve())])"
Replace-Once '        track(CP_FULL)' "        track(CP_FULL, parent_validation['hashes_before'][str(CP_FULL.resolve())])"
Replace-Once "        track(definition,PURE_DEFINITION_SHA)" @'
        track(definition,PURE_DEFINITION_SHA)
        original_benchmark = benchmark_root / 'bm_pickle/run_benchmark.py'
        track(original_benchmark, '31c0e30be79514b45db0d4e858632eb429b0fea3edbac6463ad5f34d92f3cab8')
        assert "sys.modules['_pickle'] = None" in original_benchmark.read_text(encoding='utf-8')
'@
Replace-Once "'preserved_cp_metadata':pure_metadata(cp_document),'scope':" "'preserved_cp_metadata':pure_metadata(cp_document), 'blocked_pickle': 'Pinned original --pure-python branch sets sys.modules[_pickle]=None and rejects accelerated pickle', 'scope':"
Replace-Once '        assert cpp_text.count(''#include "class_method_annotation_capture_cases.h"'') == 1' @'
        assert cpp_text.count('#include "frame_locals_retirement_cases.h"') == 1
        assert cpp_text.count('xlang3::test::check_frame_locals_retirement(result);') == 1
        assert cpp_text.count('#include "class_method_annotation_capture_cases.h"') == 1
'@
Replace-Range '        for name, (source, expected) in fixtures.items():' '        regex = ' @'
        # The exact current ten-phase focused receipt already passed. Do not
        # rerun it here; all cross-suite coverage below executes fresh.
'@
Replace-Once "        record['correctness_passed'] = True" "        record['correctness_passed'] = True`n        assert all(Path(path).is_file() and digest(path) == sha for path, sha in tracked.items())`n        assert {path.relative_to(CANDIDATE.parent).as_posix(): digest(path) for path in CANDIDATE.parent.rglob('*') if path.is_file()} == {Path(path).relative_to(CANDIDATE.parent.relative_to(ROOT)).as_posix(): sha for path, sha in record['binaries_sha256'].items()}`n        assert {path.relative_to(BASELINE.parent).as_posix(): digest(path) for path in BASELINE.parent.rglob('*') if path.is_file()} == baseline"
Replace-Once "        if not record['release_tree_unchanged']: record['status'] = 'failed_hash_integrity'" "        if not record['release_tree_unchanged']: record['status'] = 'failed_hash_integrity'`n        record['baseline_tree_unchanged'] = {path.relative_to(BASELINE.parent).as_posix(): digest(path) for path in BASELINE.parent.rglob('*') if path.is_file()} == record.get('baseline_sha256')`n        record['preserved_parent_tree_unchanged'] = {path.relative_to(PRESERVED).as_posix(): digest(path) for path in PRESERVED.rglob('*') if path.is_file()} == locals().get('preserved_tree_before')`n        if not record['baseline_tree_unchanged'] or not record['preserved_parent_tree_unchanged']: record['status'] = 'failed_hash_integrity'"
# Fragments above omit trailing newlines deliberately; ensure they cannot join the next statement.
$text = $text.Replace("    parser.add_argument('--prefix', required=True)    parser.add_argument", "    parser.add_argument('--prefix', required=True)`n    parser.add_argument")
$text = $text.Replace("correctness_reused=False)        for name, sha", "correctness_reused=False)`n        for name, sha")
$text = $text.Replace("track(ROOT / expected, expected_sha)        track(BASELINE,", "track(ROOT / expected, expected_sha)`n        track(BASELINE,")
[IO.File]::WriteAllText((Join-Path $repoRoot $target),$text,[Text.UTF8Encoding]::new($false))
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $base).Hash.ToLowerInvariant() -ne $baseHash) { throw 'Preserved parent changed' }
Get-FileHash -Algorithm SHA256 -LiteralPath $target
