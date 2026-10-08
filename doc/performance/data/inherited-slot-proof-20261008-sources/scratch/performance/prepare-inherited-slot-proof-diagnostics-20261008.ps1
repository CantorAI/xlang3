$ErrorActionPreference = 'Stop'
$repo = 'D:\CantorAI\xlang3'
$utf8 = [System.Text.UTF8Encoding]::new($false)
$pairs = @(
 @('scratch/performance/check-canonical-slot-r4-20261008.py', 'scratch/performance/check-inherited-slot-proof-20261008.py'),
 @('scratch/performance/compare-canonical-slot-r4-20261008.py', 'scratch/performance/compare-inherited-slot-proof-20261008.py'),
 @('scratch/performance/canonical_slot_r4_support_20261008.py', 'scratch/performance/inherited_slot_proof_support_20261008.py'))
$provenanceRel = 'scratch/performance/inherited-slot-proof-diagnostic-scripts-20261008-provenance.json'
foreach ($entry in $pairs) { if (Test-Path -LiteralPath (Join-Path $repo $entry[1])) { throw "Fresh script exists: $($entry[1])" } }
if (Test-Path -LiteralPath (Join-Path $repo $provenanceRel)) { throw 'Fresh diagnostic provenance exists' }
function Replace-Exact([string] $body, [string] $old, [string] $new) {
 $at = $body.IndexOf($old, [System.StringComparison]::Ordinal)
 if ($at -lt 0 -or $body.IndexOf($old, $at + $old.Length, [System.StringComparison]::Ordinal) -ge 0) { throw 'Nonunique diagnostic replacement' }
 return $body.Remove($at, $old.Length).Insert($at, $new)
}
foreach ($entry in $pairs) {
 $body = [System.IO.File]::ReadAllText((Join-Path $repo $entry[0]), $utf8).Replace("`r`n", "`n")
 $body = $body.Replace('canonical-slot-r4-early-20261008', 'inherited-slot-proof-early-20261008')
 $body = $body.Replace('canonical-slot-r4-paired-20261008', 'inherited-slot-proof-paired-20261008')
 $body = $body.Replace('check-canonical-slot-r4-20261008.py', 'check-inherited-slot-proof-20261008.py')
 $body = $body.Replace('compare-canonical-slot-r4-20261008.py', 'compare-inherited-slot-proof-20261008.py')
 $body = $body.Replace('canonical_slot_r4_support_20261008', 'inherited_slot_proof_support_20261008')
 $body = $body.Replace('vm-captured-lookup-checkpoint-20261007', 'canonical-slot-checkpoint-20261008')
 $body = $body.Replace('R4', 'Inherited-slot proof')
 if ($entry[1].EndsWith('inherited_slot_proof_support_20261008.py')) {
  $start = $body.IndexOf('SOURCES = (', [System.StringComparison]::Ordinal)
  $end = $body.IndexOf("`n)`n", $start, [System.StringComparison]::Ordinal) + 3
  if ($start -lt 0 -or $end -le $start) { throw 'Source tuple missing' }
  $sourceTuple = @'
SOURCES = (
    'src/internal/xlang3/object_model.h',
    'src/runtime/object_model.cpp',
    'src/builtins/object_type_builtins.cpp',
    'src/executor/xlang_vm/ops/xlang_vm_ops_construct.h',
    'src/executor/xlang_vm/xlang_vm_inline_support.h',
    'src/executor/xlang_vm/xlang_vm_attr.cpp',
    'src/serialize/value_graph_reader.cpp',
    'src/runtime/modules/system/weakref_module.cpp',
    'tests/cpp/canonical_slot_read_cases.h',
    'tests/fixtures/core/canonical_slot_reads.py',
    'tests/fixtures/expected/canonical_slot_reads.out',
    'tests/cpp/interpreter_tests.cpp', 'tests/run_fixtures.py',
    'src/executor/xlang_vm/ops/xlang_vm_ops_attr.h',
    'scratch/performance/canonical-slot-probe-20261008.py',
    'scratch/performance/canonical-slot-sqlglot-probe-20261008.py',
    'scratch/performance/check-inherited-slot-proof-20261008.py',
    'scratch/performance/compare-inherited-slot-proof-20261008.py',
    'scratch/performance/inherited_slot_proof_support_20261008.py',
)
'@.Replace("`r`n", "`n")
  $body = $body.Remove($start, $end - $start).Insert($start, $sourceTuple)
  $controlFiles = @'
def preserved_control():
    path = CONTROL.with_name('preserved-release-provenance.json')
    manifest = json.loads(path.read_text(encoding='utf-8'))
    if manifest.get('accepted') is not True or manifest.get('commit') != 'e2a752f62227ef1088236c68d6a08e480323e112':
        raise CheckFailure('Require the accepted R4 checkpoint control')
    if len(manifest['files_sha256']) != 140:
        raise CheckFailure('Preserve all 140 accepted Release files')
    observed = {name: digest(CONTROL.parent / name) for name in manifest['files_sha256']}
    if observed != manifest['files_sha256']:
        raise CheckFailure('A preserved accepted-control Release file changed')
    return {'manifest_sha256': digest(path), 'files_sha256': observed}


def snapshot():
'@.Replace("`r`n", "`n")
  $body = Replace-Exact $body 'def snapshot():' $controlFiles
  $body = Replace-Exact $body "        'compatibility_hook_sha256': digest(HOOK)," "        'compatibility_hook_sha256': digest(HOOK),`n        'preserved_control_release': preserved_control(),"
 }
 [System.IO.File]::WriteAllText((Join-Path $repo $entry[1]), $body, $utf8)
}
$probePaths = @('scratch/performance/canonical-slot-probe-20261008.py', 'scratch/performance/canonical-slot-sqlglot-probe-20261008.py')
$referencePath = Join-Path $repo 'doc/performance/data/canonical-slot-r4-paired-20261008.json'
$reference = [System.IO.File]::ReadAllText($referencePath, $utf8) | ConvertFrom-Json
$checks = @()
foreach ($probe in $reference.probes) {
 $path = 'scratch/performance/' + $probe.filename
 $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $repo $path)).Hash.ToLowerInvariant()
 if ($hash -ne $probe.probe_sha256) { throw 'Accepted unchanged workload probe differs' }
 $checks += [ordered]@{path=$path;sha256=$hash;unchanged_from_accepted_r4=$true}
}
$inventory = foreach ($path in @(@($pairs | ForEach-Object { $_[1] }) + $probePaths + @('scratch/performance/prepare-inherited-slot-proof-diagnostics-20261008.ps1'))) {
 [ordered]@{path=$path;bytes=(Get-Item -LiteralPath (Join-Path $repo $path)).Length;sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $repo $path)).Hash.ToLowerInvariant()}
}
$record = [ordered]@{status='fresh_scripts_frozen_not_executed_by_subagent';cpython_executable='C:/Python/Python314/python.exe';required_version='3.14.7';run_directory='D:/CantorAI/xlang3';accepted_control='build-repro/controls/canonical-slot-checkpoint-20261008';early_output='doc/performance/data/inherited-slot-proof-early-20261008.json';paired_output='doc/performance/data/inherited-slot-proof-paired-20261008.json';source_targets='All 11 proposal targets, unchanged registration/hot hook guard, two original probes and three scripts';unchanged_probes=$checks;artifacts=@($inventory)}
[System.IO.File]::WriteAllText((Join-Path $repo $provenanceRel), (($record | ConvertTo-Json -Depth 6) + "`n"), $utf8)
Write-Output 'Fresh inherited diagnostic scripts prepared without Python/runtime/build execution.'
