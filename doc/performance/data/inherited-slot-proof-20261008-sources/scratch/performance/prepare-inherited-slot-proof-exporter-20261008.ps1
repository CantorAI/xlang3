$ErrorActionPreference = 'Stop'
$repo = 'D:\CantorAI\xlang3'
$utf8 = [System.Text.UTF8Encoding]::new($false)
$target = 'scratch/performance/export-inherited-slot-proof-checkpoint-20261008.py'
$provenance = 'scratch/performance/inherited-slot-proof-exporter-20261008-provenance.json'
foreach ($rel in @($target, $provenance)) { if (Test-Path -LiteralPath (Join-Path $repo $rel)) { throw 'Fresh exporter artifact already exists' } }
$reference = 'scratch/performance/export-canonical-slot-checkpoint-20261008.py'
$main = 'scratch/performance/inherited-slot-proof-export-main-20261008.py'
$body = [System.IO.File]::ReadAllText((Join-Path $repo $reference), $utf8).Replace("`r`n", "`n")
$mainAt = $body.IndexOf('def main():', [System.StringComparison]::Ordinal)
if ($mainAt -lt 0) { throw 'Reusable exporter function prefix missing' }
$body = $body.Substring(0, $mainAt)
$body = $body.Replace('Export saved canonical-slot evidence only', 'Export saved inherited-slot proof evidence only')
$body = $body.Replace("PREFIX = 'canonical-slot-checkpoint-20261008'", "PREFIX = 'inherited-slot-proof-checkpoint-20261008'")
$old = @'
    if stage == 'R4':
        require(record.get('terminal_record') and record.get('hashes_unchanged') and
                record['hashes_before'] == record['hashes_after'], 'R4 hashes changed or terminal receipt absent')
'@.Replace("`r`n", "`n")
$new = @'
    require(record.get('terminal_record') and record.get('hashes_unchanged') and
            record['hashes_before'] == record['hashes_after'], 'Candidate hashes changed or terminal receipt absent')
'@.Replace("`r`n", "`n")
if (!$body.Contains($old)) { throw 'Reusable terminal guard differs' }
$body = $body.Replace($old, $new)
$body = $body.Replace("'acceptance': 'rejected trial: inherited regression' if stage == 'R3' else 'pending validation'", "'acceptance': 'pending full validation'")
$oldSpread = "        'sample_standard_deviation_seconds': statistics.stdev(samples),"
$newSpread = @'
        'sample_standard_deviation_seconds': statistics.stdev(samples),
        'coefficient_of_variation': statistics.stdev(samples) / statistics.fmean(samples),
        'minimum_seconds': min(samples), 'maximum_seconds': max(samples),
'@.Replace("`r`n", "`n")
if (!$body.Contains($oldSpread)) { throw 'Reusable official variation fields differ' }
$body = $body.Replace($oldSpread, $newSpread)
$body += [System.IO.File]::ReadAllText((Join-Path $repo $main), $utf8).Replace("`r`n", "`n")
[System.IO.File]::WriteAllText((Join-Path $repo $target), $body, $utf8)
$inventory = foreach ($rel in @($target, $reference, $main, 'scratch/performance/prepare-inherited-slot-proof-exporter-20261008.ps1')) {
 [ordered]@{path=$rel;bytes=(Get-Item -LiteralPath (Join-Path $repo $rel)).Length;sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $repo $rel)).Hash.ToLowerInvariant()}
}
$record = [ordered]@{status='scratch_only_exporter_prepared_not_executed';default_output='scratch/performance/inherited-slot-proof-report-preview-20261008';publication_output='doc/performance';expected_diagnostic_samples=300;expected_diagnostic_rows=4;scope='Inherited R3 diagnostics separate from original official CPython/accepted R4/candidate observations';artifacts=@($inventory)}
[System.IO.File]::WriteAllText((Join-Path $repo $provenance), (($record | ConvertTo-Json -Depth 5) + "`n"), $utf8)
Write-Output 'Inherited evidence exporter prepared without export/Python/runtime/build execution.'
