param(
    [Parameter(Mandatory = $true)][string]$XLang3,
    [string]$ProductionPackages = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'scratch\fastapi-deps'),
    [string]$TestPackages = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'scratch\fastapi-test-deps'),
    [string]$UpstreamRoot = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'scratch\upstream-compat'),
    [string]$ResultsDirectory = (Join-Path (Split-Path -Parent (Split-Path -Parent $PSScriptRoot)) 'scratch\upstream-results'),
    [string[]]$Project,
    [int]$MaxFailures = 20,
    [switch]$StopOnFailure
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$manifestPath = Join-Path $PSScriptRoot 'upstream-matrix.json'
$matrix = @(Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json)
if ($Project.Count -gt 0) {
    $unknown = @($Project | Where-Object { $_ -notin $matrix.name })
    if ($unknown.Count -gt 0) {
        throw "Unknown upstream matrix project(s): $($unknown -join ', ')"
    }
    $matrix = @($matrix | Where-Object { $_.name -in $Project })
}

$xlangPath = (Resolve-Path -LiteralPath $XLang3).Path
$productionPath = (Resolve-Path -LiteralPath $ProductionPackages).Path
$testPackagesPath = (Resolve-Path -LiteralPath $TestPackages).Path
$upstreamPath = (Resolve-Path -LiteralPath $UpstreamRoot).Path
New-Item -ItemType Directory -Force -Path $ResultsDirectory | Out-Null
$resultsPath = Join-Path $ResultsDirectory 'matrix-results.json'

$runtimeName = ((& $xlangPath -c 'import sys; print(sys.implementation.name)' | Out-String) -replace "`r`n", "`n").Trim()
if ($LASTEXITCODE -ne 0 -or $runtimeName -ne 'xlang3') {
    throw "Upstream matrix must run on XLang3; preflight returned '$runtimeName'"
}

$previousPythonPath = $env:PYTHONPATH
$previousCoverageFile = $env:COVERAGE_FILE
$results = @()
try {
    foreach ($entry in $matrix) {
        $checkout = Join-Path $upstreamPath $entry.checkout
        if (-not (Test-Path -LiteralPath $checkout -PathType Container)) {
            throw "Missing upstream checkout for $($entry.name): $checkout"
        }
        if ($null -ne $entry.commit) {
            $actualCommit = (& git -C $checkout rev-parse HEAD | Out-String).Trim()
            if ($LASTEXITCODE -ne 0 -or $actualCommit -ne $entry.commit) {
                throw "$($entry.name) checkout is not at pinned commit $($entry.commit); got '$actualCommit'"
            }
            $dirty = (& git -C $checkout status --porcelain | Out-String).Trim()
            if ($LASTEXITCODE -ne 0 -or $dirty.Length -ne 0) {
                throw "$($entry.name) checkout contains local changes"
            }
        }

        $sourcePath = (Resolve-Path -LiteralPath (Join-Path $checkout $entry.source)).Path
        $testPath = (Resolve-Path -LiteralPath (Join-Path $checkout $entry.tests)).Path
        $env:PYTHONPATH = "$testPackagesPath;$productionPath;$sourcePath"
        $env:COVERAGE_FILE = Join-Path $ResultsDirectory ".coverage-$($entry.name)"
        $logPath = Join-Path $ResultsDirectory "$($entry.name).log"
        $started = Get-Date
        Write-Host "upstream matrix $($entry.name) $($entry.version)"
        Push-Location -LiteralPath $checkout
        try {
            & $xlangPath -m pytest $testPath `
                "--maxfail=$MaxFailures" `
                --assert=plain `
                -p no:logging `
                --timeout=0 `
                -W 'ignore:The anyio.abc.BlockingPortal alias is deprecated:DeprecationWarning' 2>&1 |
                Tee-Object -LiteralPath $logPath
            $exitCode = $LASTEXITCODE
        } finally {
            Pop-Location
        }
        $finished = Get-Date
        $results += [pscustomobject]@{
            project = $entry.name
            version = $entry.version
            commit = $entry.commit
            status = if ($exitCode -eq 0) { 'passed' } else { 'failed' }
            exit_code = $exitCode
            started_at = $started.ToUniversalTime().ToString('o')
            finished_at = $finished.ToUniversalTime().ToString('o')
            duration_seconds = [math]::Round(($finished - $started).TotalSeconds, 3)
            log = $logPath
        }
        $results | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $resultsPath -Encoding utf8
        if ($exitCode -ne 0 -and $StopOnFailure) {
            break
        }
    }
} finally {
    $env:PYTHONPATH = $previousPythonPath
    $env:COVERAGE_FILE = $previousCoverageFile
}

$failed = @($results | Where-Object status -eq 'failed')
if ($failed.Count -gt 0) {
    Write-Error "Upstream matrix failed: $($failed.project -join ', ')"
    exit 1
}
Write-Host "upstream matrix passed: $($results.project -join ', ')"
