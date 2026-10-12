$ErrorActionPreference = 'Stop'
$repo = 'D:\CantorAI\xlang3'
$folderRel = 'scratch/performance/inherited-slot-proof-git-lf-20261008-sources'
$provRel = 'scratch/performance/inherited-slot-proof-git-lf-20261008-provenance.json'
$folder = Join-Path $repo $folderRel
$provPath = Join-Path $repo $provRel
if ((Test-Path -LiteralPath $folder) -or (Test-Path -LiteralPath $provPath)) { throw 'Fresh LF provenance already exists' }
$utf8 = [System.Text.UTF8Encoding]::new($false, $true)
$paired = Get-Content -Raw -LiteralPath (Join-Path $repo 'doc/performance/data/inherited-slot-proof-paired-20261008.json') | ConvertFrom-Json
$proposal = Get-Content -Raw -LiteralPath (Join-Path $repo 'scratch/performance/inherited-slot-proof-proposal-r2-20261008-provenance.json') | ConvertFrom-Json
if ($paired.status -ne 'terminal' -or !$paired.hashes_unchanged -or $proposal.targets.Count -ne 11) { throw 'Need terminal paired evidence and 11 proposal targets' }
$inventory = foreach ($target in $proposal.targets) {
    $rel = $target.path
    $source = Join-Path $repo $rel
    $workingHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $source).Hash.ToLowerInvariant()
    if ($workingHash -ne $paired.hashes_before.source_sha256.$rel) { throw "Compiled working source moved: $rel" }
    $raw = [System.IO.File]::ReadAllBytes($source)
    $normalized = $utf8.GetBytes($utf8.GetString($raw).Replace("`r`n", "`n"))
    $destination = Join-Path $folder $rel
    [System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName($destination)) | Out-Null
    [System.IO.File]::WriteAllBytes($destination, $normalized)
    $filteredBlob = (& git hash-object "--path=$rel" -- $source).Trim()
    if ($LASTEXITCODE -ne 0) { throw "Read-only Git filtered hash failed: $rel" }
    $lfBlob = (& git hash-object --no-filters -- $destination).Trim()
    if ($LASTEXITCODE -ne 0 -or $filteredBlob -ne $lfBlob) { throw "LF bytes do not match Git clean-filter identity: $rel" }
    $after = (Get-FileHash -Algorithm SHA256 -LiteralPath $source).Hash.ToLowerInvariant()
    if ($after -ne $workingHash) { throw "Working source moved during LF provenance: $rel" }
    [ordered]@{path=$rel;working_sha256=$workingHash;working_bytes=$raw.Length;git_lf_sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath $destination).Hash.ToLowerInvariant();git_lf_bytes=$normalized.Length;git_clean_blob_sha1=$filteredBlob;git_lf_blob_sha1=$lfBlob;git_lf_copy="$folderRel/$rel"}
}
$record = [ordered]@{status='scratch_only_readonly_git_lf_provenance';policy='Exact compiled working bytes remain authoritative; separate CRLF-to-LF copies were verified against read-only Git clean-filter blob identity without staging or modifying sources';target_count=11;paired_evidence_sha256=(Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $repo 'doc/performance/data/inherited-slot-proof-paired-20261008.json')).Hash.ToLowerInvariant();targets=@($inventory)}
[System.IO.File]::WriteAllText($provPath, (($record | ConvertTo-Json -Depth 7) + "`n"), $utf8)
Write-Output 'Prepared 11 separate Git-clean LF copies; actual sources and Git index unchanged.'
