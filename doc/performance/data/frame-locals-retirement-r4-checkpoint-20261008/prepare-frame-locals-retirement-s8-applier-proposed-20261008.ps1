$ErrorActionPreference = 'Stop'
$repoRoot = 'D:/CantorAI/xlang3'
Set-Location -LiteralPath $repoRoot
$base = 'scratch/performance/apply-dict-init-merge-s8-correctness-proposed-20261008.py'
$target = 'scratch/performance/apply-frame-locals-retirement-r4-s8-proposed-20261008.py'
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $base).Hash.ToLowerInvariant() -ne '989cdebefeb9ff43d947daf9a01814bcf60c4da3451140f82af74182e6257d18') { throw 'Reviewed applier base changed' }
if (Test-Path -LiteralPath $target) { throw 'Refusing controller overwrite' }
$text=[IO.File]::ReadAllText((Join-Path $repoRoot $base)).Replace("`r`n","`n")
function Replace-Once([string]$old,[string]$new) { if (($script:text.Split(@($old),[StringSplitOptions]::None)).Count -ne 2) { throw ('Nonunique boundary: '+$old) }; $script:text=$script:text.Replace($old,$new) }
$header=@'
"""Root-only three-file frame-retirement correctness application over S8.

No build, test, benchmark, staging or acceptance. Preserve complete source110
and current Release178 before only two existing writes and one new helper.
Strict application idle guards include MSBuild; no untimed logging exception.
"""
'@
$headerEnd=$text.IndexOf('import argparse',[StringComparison]::Ordinal)
$text=$header.Replace("`r`n","`n")+"`n"+$text.Substring($headerEnd)
Replace-Once "PROOF = ROOT / 'scratch/performance/dict-init-merge-correctness-s8-proposed-20261008-provenance.json'" "PROOF = ROOT / 'scratch/performance/frame-locals-retirement-r4-proposed-20261008-provenance.json'"
Replace-Once "PROOF_SHA = '4da9775c8affcd678945d490608c3e09edbf00a0a64614a64293b5eb2fffe00e'" "PROOF_SHA = '24b1c58347f3bea0c4bc40d5e450efb04c8ff614f4c0b482cc12511380cf8e76'"
Replace-Once "OWNED = ('src/builtins/object_type_builtins.cpp', 'tests/run_fixtures.py', 'tests/run_fixtures.ps1')" "OWNED = ('src/runtime/runtime.cpp', 'tests/cpp/interpreter_tests.cpp')"
$old=@'
NEW = ('tests/fixtures/core/dict_native_semantic_prerequisites.py',
       'tests/fixtures/expected/dict_native_semantic_prerequisites.out')
'@
Replace-Once ($old.Replace("`r`n","`n")) "NEW = ('tests/cpp/frame_locals_retirement_cases.h',)"
Replace-Once "rollback = safe(ROOT, 'scratch/performance/' + args.prefix + '-owned-parent-raw')" "preserved = safe(ROOT, 'build-repro/controls/' + args.prefix)"
Replace-Once 'assert not rollback.exists()' 'assert not preserved.exists()'
Replace-Once "patch = pin(safe(ROOT, proof['patch']), proof['patch_sha256'])" "patch = pin(ROOT / 'scratch/performance/frame-locals-retirement-r4-proposed-20261008.patch', proof['patch_sha256'])"
$start=$text.IndexOf("    pin(safe(ROOT, proof['preserved_original_provenance'])",[StringComparison]::Ordinal)
$end=$text.IndexOf('    candidates = safe(ROOT,',[StringComparison]::Ordinal)
if ($start -lt 0 -or $end -le $start) { throw 'Reference preflight span absent' }
$preflight=@'
    base, restore = READ(base_path), READ(restore_path)
    source = base['source_sha256']
    assert len(source) == 110 and source == proof['raw_before_sha256']
    assert restore['terminal'] and restore['status'] == 'withdrawn_unmeasured_duplicate_r10_preserved_exact_s8_restored'
    assert restore['restored_source_sha256'] == source and restore['s8_manifest_sha256'] == proof['restored_parent_control_manifest_sha256']
    assert restore['accepted_gate_baseline_changed'] is False and restore['unrelated_changes_preserved']
    original = READ(pin(DATA / 'runtime-frame-context-cpython3147-s8-correctness-r2-20261008.json',
        '159f7294d3ff4e1395722145e3bc8ef72a6ed47834989798cd588af0b5af7d41'))
    retained = READ(pin(DATA / 'frame-locals-retained-cpython3147-s8-reference-20261008.json',
        '71a0288e97ffb357de8e447a7a9fca38450fb173201b708988d74341d7150751'))
    for reference, expected_count in ((original, 1), (retained, 2)):
        assert reference['terminal'] and reference['hashes_unchanged']
        cp_rows = [row for row in reference['phases'] if row['runtime'] == 'cpython3147']
        assert len(cp_rows) == expected_count
        assert all(row['passed'] and row['exit_code'] == 0 and not row['timeout'] and row['output_matches_expected'] for row in cp_rows)
        for row in reference['phases']:
            for stream in ('stdout', 'stderr'): pin(safe(DATA, row[stream + '_log']), row[stream + '_sha256'])
    assert original['source_sha256'] == proof['strict_reference_source_sha256']
    assert original['expected_sha256'] == proof['strict_reference_expected_sha256'] and original['groups_expected'] == 4
    assert [(item['name'], item['groups_expected']) for item in retained['fixtures']] == [('retained', 1), ('extra', 1)]
    for item, prefix in zip(retained['fixtures'], ('retained_mapping', 'retained_extra')):
        assert item['source_sha256'] == proof[prefix + '_fixture_sha256'] and item['expected_sha256'] == proof[prefix + '_expected_sha256']
    for name in ('strict_reference_source', 'strict_reference_expected', 'retained_mapping_fixture',
                 'retained_mapping_expected', 'retained_extra_fixture', 'retained_extra_expected'):
        pin(safe(ROOT, proof[name]), proof[name + '_sha256'])
    assert tuple(proof['existing_owned_targets']) == OWNED and tuple(proof['new_owned_targets']) == NEW
    assert set(proof['candidate_source_sha256']) == set(OWNED + NEW)
    for path, value in source.items():
        pin(safe(ROOT, path), value)
        pin(safe(safe(ROOT, proof['raw_input_root']), path), value)
    for path in NEW: assert not safe(ROOT, path).exists()
'@
$text=$text.Substring(0,$start)+$preflight.Replace("`r`n","`n")+"`n"+$text.Substring($end)
$old=@'
    for field in ('raw', 'normalized'):
        directory = safe(ROOT, proof[field + '_input_root'])
        for path, value in proof[field + '_input_sha256'].items(): pin(safe(directory, path), value)
'@
Replace-Once ($old.Replace("`r`n","`n")) ''
$text=$text.Replace("proof['control_manifest_sha256']","proof['restored_parent_control_manifest_sha256']")
Replace-Once "owned_targets=list(OWNED + NEW), additional_parent_source_sha256=proof['additional_native_source_sha256']," 'owned_targets=list(OWNED + NEW),'
$start=$text.IndexOf("        idle('before-owned-backup'); assert stable()",[StringComparison]::Ordinal)
$end=$text.IndexOf('        for path in OWNED + NEW:',[StringComparison]::Ordinal)
if ($start -lt 0 -or $end -le $start) { throw 'Preservation span absent' }
$preservation=@'
        idle('before-complete-parent-preservation'); assert stable()
        preserved.mkdir()
        for directory, prefix, mapping in ((RELEASE, '', release), (ROOT, 'source-snapshot/', source),
                                           (ROOT, 'unrelated-dirty-snapshot/', unrelated)):
            for path, value in mapping.items():
                target = safe(preserved, prefix + path); target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(safe(directory, path), target); assert SHA(target) == value
        preserved_map = dict(release, **{'source-snapshot/' + p: h for p, h in source.items()},
            **{'unrelated-dirty-snapshot/' + p: h for p, h in unrelated.items()})
        assert tree(preserved) == preserved_map and stable()
        manifest = safe(preserved, 'preserved-release-provenance.json')
        manifest.write_bytes((json.dumps(dict(status='preserved_restored_s8_before_unvalidated_frame_retirement_trial',
            terminal=True, accepted=False, performance_measured=False, files_sha256=release, file_count=178,
            source_snapshot_sha256=source, source_count=110, unrelated_dirty_snapshot_sha256=unrelated,
            source_inventory_sha256=SHA(base_path), actual_restoration_receipt_sha256=SHA(restore_path),
            proposal_sha256=PROOF_SHA, baseline_sha256=baseline), indent=2) + '\n').encode('utf-8'))
        preserved_map['preserved-release-provenance.json'] = SHA(manifest)
        record.update(preserved_parent=str(preserved), preserved_manifest_sha256=SHA(manifest))
        save()
        check = subprocess.run(['git', 'apply', '--check', str(patch)], cwd=ROOT, capture_output=True)
        assert check.returncode == 0, check.stderr.decode('utf-8', 'replace')
        idle('before-three-owned-writes'); assert stable() and tree(preserved) == preserved_map
        record.update(mutation_started=True, status='applying_owned_frame_retirement_sources')
        save()
'@
$text=$text.Substring(0,$start)+$preservation.Replace("`r`n","`n")+"`n"+$text.Substring($end)
Replace-Once "assert len(after) == proof['merged_source_count_after_exact_five_writes'] == 113" 'assert len(after) == 111'
Replace-Once "assert tree(rollback) == proof['actual_input_sha256'] and unrelated_dirty() == unrelated" 'assert tree(preserved) == preserved_map and unrelated_dirty() == unrelated'
$text=$text.Replace('applied_unbuilt_unvalidated_dict_init_merge_correctness','applied_unbuilt_unvalidated_frame_locals_retirement_correctness')
Replace-Once 'strict_four_group_expectations_unchanged=True' 'strict_original_four_retained_one_extra_one_expectations_unchanged=True'
[IO.File]::WriteAllText((Join-Path $repoRoot $target),$text,[Text.UTF8Encoding]::new($false))
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $base).Hash.ToLowerInvariant() -ne '989cdebefeb9ff43d947daf9a01814bcf60c4da3451140f82af74182e6257d18') { throw 'Preserved base changed' }
Get-FileHash -Algorithm SHA256 -LiteralPath $target
