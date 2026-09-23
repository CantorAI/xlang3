param(
    [Parameter(Mandatory = $true)][string]$XLang3,
    [string]$ProductionPackages,
    [string]$TestPackages,
    [string]$UpstreamRoot,
    [string]$ResultsDirectory,
    [string[]]$Project,
    [int]$MaxFailures = 20,
    [int]$TimeoutSeconds = 120,
    [int]$Workers = 0,
    [switch]$StopOnFailure
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $ProductionPackages) {
    $ProductionPackages = Join-Path $repoRoot 'scratch\fastapi-deps'
}
if (-not $TestPackages) {
    $TestPackages = Join-Path $repoRoot 'scratch\fastapi-test-deps'
}
if (-not $UpstreamRoot) {
    $UpstreamRoot = Join-Path $repoRoot 'scratch\upstream-compat'
}
if (-not $ResultsDirectory) {
    $ResultsDirectory = Join-Path $repoRoot 'scratch\upstream-results'
}
$manifestPath = Join-Path $PSScriptRoot 'upstream-matrix.json'
$matrix = @()
foreach ($entry in (Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json)) {
    $matrix += $entry
}
$availableProjects = @($matrix | ForEach-Object { $_.name })
$platformSkips = @()
foreach ($entry in (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'upstream-platform-skips.json') -Raw | ConvertFrom-Json)) {
    $platformSkips += $entry
}
if ($Project.Count -gt 0) {
    $unknown = @($Project | Where-Object { $_ -notin $matrix.name })
    if ($unknown.Count -gt 0) {
        throw "Unknown upstream matrix project(s): $($unknown -join ', ')"
    }
    $selected = @()
    foreach ($entry in $matrix) {
        foreach ($requested in $Project) {
            if ([string]::Equals($entry.name, $requested,
                    [System.StringComparison]::OrdinalIgnoreCase)) {
                $selected += $entry
                break
            }
        }
    }
    $matrix = $selected
}
if ($matrix.Count -eq 0) {
    throw "Upstream matrix selected no projects (requested: $($Project -join ', '); available: $($availableProjects -join ', '); raw entries: $($availableProjects.Count))"
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
$previousPycachePrefix = $env:PYTHONPYCACHEPREFIX
$env:PYTHONPYCACHEPREFIX = Join-Path $ResultsDirectory (
    'pycache-' + (Get-Date).ToUniversalTime().ToString('yyyyMMddTHHmmssfffffff') + '-' + $PID
)
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
        $entrySkips = @()
        $deselectArgs = @()
        if ($env:OS -eq 'Windows_NT') {
            $entrySkips = @($platformSkips | Where-Object { $_.project -eq $entry.name -and $_.platform -eq 'win32' })
            foreach ($skip in $entrySkips) {
                $deselectArgs += "--deselect=$($skip.node)"
                Write-Host "upstream platform deselection $($skip.node): $($skip.reason)"
            }
        }
        Push-Location -LiteralPath $checkout
        try {
            $previousErrorActionPreference = $ErrorActionPreference
            $ErrorActionPreference = 'Continue'
            & $xlangPath -m pytest $testPath `
                "--maxfail=$MaxFailures" `
                -n $Workers `
                @deselectArgs `
                --assert=plain `
                "--timeout=$TimeoutSeconds" `
                -W 'ignore:Class-scoped fixture defined as instance method is deprecated:pytest.PytestRemovedIn10Warning' `
                -W 'ignore:The anyio.abc.BlockingPortal alias is deprecated:DeprecationWarning' 2>&1 |
                Tee-Object -LiteralPath $logPath
            $exitCode = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = $previousErrorActionPreference
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
            platform_deselections = $entrySkips
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
    $env:PYTHONPYCACHEPREFIX = $previousPycachePrefix
}

$failed = @($results | Where-Object status -eq 'failed')
if ($failed.Count -gt 0) {
    Write-Error "Upstream matrix failed: $($failed.project -join ', ')"
    exit 1
}
Write-Host "upstream matrix passed: $($results.project -join ', ')"
