# Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
param(
    [string]$Section = "",
    [int]$Limit = 3,
    [int]$Iterations = 1,
    [string]$CodexCommand = "",
    [string]$CommitMessage = "Advance Python 3.14 compatibility",
    [switch]$Run,
    [switch]$Status,
    [switch]$DryRun,
    [switch]$NoCommit,
    [switch]$SkipBuild,
    [switch]$SkipTests,
    [switch]$ResetLoopState
)

$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$python = "C:\Python\Python314\python.exe"
if (-not (Test-Path -LiteralPath $python)) {
    $python = "python"
}

$argsList = @(
    "agent\scripts\codex_loop.py",
    "--goal", "python314_compat",
    "--limit", "$Limit",
    "--iterations", "$Iterations"
)

if ($Section) {
    $argsList += @("--section", $Section)
}
if ($CodexCommand) {
    $argsList += @("--codex-command", $CodexCommand)
}
if ($CommitMessage) {
    $argsList += @("--commit-message", $CommitMessage)
}
if ($Status) {
    $argsList += "--status"
}
if ($DryRun) {
    $argsList += "--dry-run"
}
if ($NoCommit) {
    $argsList += "--no-commit"
}
if ($SkipBuild) {
    $argsList += "--skip-build"
}
if ($SkipTests) {
    $argsList += "--skip-tests"
}
if ($ResetLoopState) {
    $argsList += "--reset-loop-state"
}

$hasRunMode = $Run -or $Status -or $DryRun -or $CodexCommand
if (-not $hasRunMode) {
    Write-Host "No Codex backend command was provided; showing status."
    Write-Host "For a real batch, pass -Run with [codex].command in agent\config.toml, or pass -CodexCommand."
    $argsList += "--status"
}

Push-Location $root
try {
    & $python @argsList
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
