param(
    [Parameter(Mandatory=$true)][string]$Manifest,
    [Parameter(Mandatory=$true)][string]$ManifestSha256,
    [switch]$Publish
)
$ErrorActionPreference = 'Stop'
$taskRoot = [IO.Path]::GetFullPath('D:/CantorAI/xlang3')
function Get-TaskHash([string]$path) {
    return (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
}
function Get-TaskPath([string]$relative) {
    if ($relative -match '[\\:]' -or $relative.StartsWith('/') -or
        @($relative.Split('/') | Where-Object { $_ -in @('', '.', '..') }).Count) {
        throw "Unsafe repository path: $relative"
    }
    $path = $taskRoot
    foreach ($part in $relative.Split('/')) {
        $path = Join-Path $path $part
        if ((Test-Path -LiteralPath $path) -and
            ((Get-Item -LiteralPath $path -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) {
            throw "Reparse point rejected: $path"
        }
    }
    $path = [IO.Path]::GetFullPath($path)
    if (-not $path.StartsWith($taskRoot + '\', [StringComparison]::OrdinalIgnoreCase)) { throw 'Path escapes repository' }
    return $path
}
function Read-TaskGit([string[]]$arguments) {
    $start = [Diagnostics.ProcessStartInfo]::new('git')
    $start.WorkingDirectory = $taskRoot
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.ArgumentList.Add('--no-optional-locks')
    foreach ($argument in $arguments) { $start.ArgumentList.Add($argument) }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    [void]$process.Start()
    $bytes = [IO.MemoryStream]::new()
    $process.StandardOutput.BaseStream.CopyTo($bytes)
    $errorText = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw $errorText }
    return [Text.Encoding]::UTF8.GetString($bytes.ToArray())
}
function Assert-TaskIdentity {
    foreach ($guard in $taskPlan.guards.PSObject.Properties) {
        if ((Get-TaskHash (Get-TaskPath $guard.Value.path)) -ne $guard.Value.sha256) { throw "Guard changed: $($guard.Name)" }
    }
    $inventory = Get-Content -Raw -LiteralPath (Get-TaskPath $taskPlan.guards.registered_source.path) | ConvertFrom-Json
    if (@($inventory.source_sha256.PSObject.Properties).Count -ne 111) { throw 'Expected compiled source111' }
    foreach ($entry in $inventory.source_sha256.PSObject.Properties) {
        if ((Get-TaskHash (Get-TaskPath $entry.Name)) -ne $entry.Value) { throw "Live source drift: $($entry.Name)" }
    }
    $full = Get-Content -Raw -LiteralPath (Get-TaskPath $taskPlan.guards.full_validation.path) | ConvertFrom-Json
    if (-not $full.terminal -or $full.status -ne 'validated' -or -not $full.full_validated -or
        -not $full.correctness_passed -or -not $full.hashes_unchanged -or -not $full.release_tree_unchanged -or
        -not $full.baseline_tree_unchanged -or -not $full.preserved_parent_tree_unchanged -or
        $full.fixed_gate.exit_code -ne 0 -or -not $full.official_pickle_pure_python.complete -or
        $full.official_pickle_pure_python.values_count.pickle_pure_python -ne 20) { throw 'Actual complete R4 validation absent' }
    if (@($full.source_sha256.PSObject.Properties).Count -ne 111 -or
        @($full.binaries_sha256.PSObject.Properties).Count -ne 178 -or
        @($full.baseline_sha256.PSObject.Properties).Count -ne 177) { throw 'Compiled identity counts changed' }
    foreach ($entry in $full.source_sha256.PSObject.Properties) {
        if ($inventory.source_sha256.($entry.Name) -ne $entry.Value) { throw 'Source inventories differ' }
    }
    foreach ($entry in $full.binaries_sha256.PSObject.Properties) {
        if ((Get-TaskHash (Get-TaskPath $entry.Name)) -ne $entry.Value) { throw 'Current Release changed' }
    }
    foreach ($entry in $full.baseline_sha256.PSObject.Properties) {
        if ((Get-TaskHash (Get-TaskPath ('build-repro/Release/' + $entry.Name))) -ne $entry.Value) { throw 'Fixed accepted baseline changed' }
    }
    if (@(Get-ChildItem -LiteralPath (Get-TaskPath 'build-repro/main-verify-20261006/Release') -Recurse -File).Count -ne 178 -or
        @(Get-ChildItem -LiteralPath (Get-TaskPath 'build-repro/Release') -Recurse -File).Count -ne 177) { throw 'Release tree membership changed' }
}

if ([IO.Path]::GetFullPath((Get-Location).Path) -ne $taskRoot) { throw 'Use repository cwd' }
if ($ManifestSha256 -notmatch '^[0-9a-f]{64}$') { throw 'Supply frozen manifest SHA256' }
$taskManifestPath = Get-TaskPath $Manifest
if ((Get-TaskHash $taskManifestPath) -ne $ManifestSha256) { throw 'Frozen manifest changed' }
$taskPlan = Get-Content -Raw -LiteralPath $taskManifestPath | ConvertFrom-Json
if ($taskPlan.status -ne 'frozen_doc_only_validated_r4_checkpoint_publication_plan' -or
    $taskPlan.row_count -ne $taskPlan.files.Count -or $taskPlan.recorded_source_count -ne 111) { throw 'Wrong publication plan' }
$taskHead = (Read-TaskGit @('rev-parse', 'HEAD')).Trim()
if ($taskHead -ne $taskPlan.parent_head) { throw 'HEAD changed' }
$taskIndexName = (Read-TaskGit @('rev-parse', '--git-path', 'index')).Trim().Replace('\','/')
$taskIndexPath = Get-TaskPath $taskIndexName
$taskIndexHash = Get-TaskHash $taskIndexPath
$taskDirty = @{}
foreach ($path in (Read-TaskGit @('diff', 'HEAD', '--name-only', '-z')).Split([char]0)) {
    if ($path) { $taskDirty[$path] = Get-TaskHash (Get-TaskPath $path) }
}
Assert-TaskIdentity
$taskDestinations = @{}
foreach ($row in $taskPlan.files) {
    if (-not $row.destination.StartsWith('doc/performance/') -or
        $row.source -match '\.(exe|dll|obj|lib|pdb|exp|pyd|pyc)$' -or $taskDestinations.ContainsKey($row.destination)) {
        throw 'Excluded or duplicate document destination'
    }
    $taskDestinations[$row.destination] = $true
    $source = Get-TaskPath $row.source
    if ((Get-TaskHash $source) -ne $row.sha256 -or (Get-Item -LiteralPath $source).Length -ne $row.bytes) { throw "Source changed: $($row.source)" }
    $destination = Get-TaskPath $row.destination
    if ((Test-Path -LiteralPath $destination) -and (Get-TaskHash $destination) -ne $row.sha256) { throw "Different destination exists: $($row.destination)" }
}
$taskSelfDestination = Get-TaskPath $taskPlan.plan_publication_destination
if (-not $taskPlan.plan_publication_destination.StartsWith('doc/performance/') -or
    $taskDestinations.ContainsKey($taskPlan.plan_publication_destination)) { throw 'Unsafe manifest destination' }
if ((Test-Path -LiteralPath $taskSelfDestination) -and (Get-TaskHash $taskSelfDestination) -ne $ManifestSha256) { throw 'Different manifest exists' }
if (-not $Publish) {
    Write-Output "Preview READY: $($taskPlan.row_count) exact document rows plus manifest; no copies/staging/source changes."
    exit 0
}
# Copy absent documents only. Every source, destination, current compiled identity
# and actual terminal result passed above; no engine/binary/index path is written.
foreach ($row in $taskPlan.files) {
    $destination = Get-TaskPath $row.destination
    if (-not (Test-Path -LiteralPath $destination)) {
        [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination))
        [IO.File]::Copy((Get-TaskPath $row.source), $destination, $false)
    }
    if ((Get-TaskHash $destination) -ne $row.sha256) { throw 'Copied document bytes differ' }
}
if (-not (Test-Path -LiteralPath $taskSelfDestination)) {
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($taskSelfDestination))
    [IO.File]::Copy($taskManifestPath, $taskSelfDestination, $false)
}
if ((Get-TaskHash $taskSelfDestination) -ne $ManifestSha256) { throw 'Copied manifest differs' }
Assert-TaskIdentity
foreach ($path in $taskDirty.Keys) {
    if ((Get-TaskHash (Get-TaskPath $path)) -ne $taskDirty[$path]) { throw "Existing dirty bytes changed: $path" }
}
if ((Get-TaskHash $taskIndexPath) -ne $taskIndexHash -or (Read-TaskGit @('rev-parse', 'HEAD')).Trim() -ne $taskHead) { throw 'Index or HEAD changed' }
Write-Output "Published and verified $($taskPlan.row_count + 1) document paths. Live111/Release178/fixedbaseline177, dirty bytes and index unchanged."
Write-Output 'Root stages this doc-only NUL list and its separate three-owned-file code selection; this copier performs no staging.'
