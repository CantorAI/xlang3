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
    [string]$BuildDir = "D:\CantorAI\xlang3\build-rp2040",
    [string]$BoardLabel = "RPI-RP2"
)

$ErrorActionPreference = "Stop"

function Find-CommandPath($Name) {
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($null -eq $cmd) {
        return $null
    }
    return $cmd.Source
}

if (-not $env:PICO_SDK_PATH) {
    throw "PICO_SDK_PATH is not set. Example: `$env:PICO_SDK_PATH = 'D:\pico\pico-sdk'"
}

if (-not (Test-Path -LiteralPath $env:PICO_SDK_PATH)) {
    throw "PICO_SDK_PATH does not exist: $env:PICO_SDK_PATH"
}

if (-not (Find-CommandPath "cmake")) {
    throw "cmake was not found on PATH"
}

if (-not (Find-CommandPath "ninja")) {
    throw "ninja was not found on PATH"
}

if (-not (Find-CommandPath "arm-none-eabi-gcc")) {
    throw "arm-none-eabi-gcc was not found on PATH"
}

$repoRoot = Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..")
$sourceDir = Join-Path $repoRoot "ports\rp2040"

cmake -S $sourceDir -B $BuildDir -G Ninja
cmake --build $BuildDir

$uf2 = Join-Path $BuildDir "xlang3_pico.uf2"
if (-not (Test-Path -LiteralPath $uf2)) {
    throw "UF2 was not produced: $uf2"
}

$board = Get-Volume |
    Where-Object { $_.FileSystemLabel -eq $BoardLabel -and $_.DriveLetter } |
    Select-Object -First 1

if ($null -eq $board) {
    throw "Board drive '$BoardLabel' was not found. Hold BOOTSEL while plugging in the Pico."
}

$target = "$($board.DriveLetter):\xlang3_pico.uf2"
Copy-Item -LiteralPath $uf2 -Destination $target -Force
Write-Host "Flashed $target"
