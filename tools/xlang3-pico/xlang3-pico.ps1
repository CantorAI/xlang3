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
    [Parameter(Position = 0)]
    [ValidateSet("init", "build", "flash", "deploy", "list")]
    [string]$Command = "build",

    [Parameter(Position = 1)]
    [string]$ProjectPath = ".",

    [string]$BuildDir = "D:\CantorAI\xlang3\build-rp2040-vs",
    [string]$BoardLabel = "RPI-RP2"
)

$ErrorActionPreference = "Stop"

function Get-RepoRoot {
    return (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..\..")).Path
}

function Invoke-PicoBuild {
    param([string]$BuildDirectory)

    $repoRoot = Get-RepoRoot
    $sourceDir = Join-Path $repoRoot "ports\rp2040"
    $vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
    $armGcc = Join-Path $env:APPDATA "xPacks\@xpack-dev-tools\arm-none-eabi-gcc\15.2.1-1.1.1\.content\bin"

    if (-not $env:PICO_SDK_PATH) {
        $env:PICO_SDK_PATH = "D:\pico\pico-sdk"
    }

    if (-not (Test-Path -LiteralPath $env:PICO_SDK_PATH)) {
        throw "PICO_SDK_PATH does not exist: $env:PICO_SDK_PATH"
    }

    if (-not (Test-Path -LiteralPath $armGcc)) {
        throw "ARM GCC was not found: $armGcc"
    }

    $command = "set `"PATH=$armGcc;%PATH%`" && call `"$vcvars`" && cmake -S `"$sourceDir`" -B `"$BuildDirectory`" -G Ninja && cmake --build `"$BuildDirectory`""
    cmd.exe /d /s /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "RP2040 build failed with exit code $LASTEXITCODE"
    }
}

function Invoke-PicoDeploy {
    param(
        [string]$BuildDirectory,
        [string]$Label
    )

    Invoke-PicoBuild -BuildDirectory $BuildDirectory
    Copy-PicoUf2 -BuildDirectory $BuildDirectory -Label $Label
}

function Copy-PicoUf2 {
    param(
        [string]$BuildDirectory,
        [string]$Label
    )

    $uf2 = Join-Path $BuildDirectory "xlang3_pico.uf2"
    if (-not (Test-Path -LiteralPath $uf2)) {
        throw "UF2 was not found: $uf2"
    }

    $board = Get-Volume |
        Where-Object { $_.FileSystemLabel -eq $Label -and $_.DriveLetter } |
        Select-Object -First 1

    if ($null -eq $board) {
        throw "Board drive '$Label' was not found. Hold BOOTSEL while plugging in the Pico."
    }

    $target = "$($board.DriveLetter):\xlang3_pico.uf2"
    Copy-Item -LiteralPath $uf2 -Destination $target -Force
    Write-Host "Flashed $target"
}

switch ($Command) {
    "init" {
        $project = Join-Path (Get-Location) $ProjectPath
        New-Item -ItemType Directory -Force -Path $project | Out-Null
        $main = Join-Path $project "main.py"
        if (-not (Test-Path -LiteralPath $main)) {
            @'
import gpio
import time

led = gpio.Pin(15, gpio.OUT)

while True:
    led.write(1)
    time.sleep_ms(1000)
    led.write(0)
    time.sleep_ms(1000)
'@ | Set-Content -LiteralPath $main -Encoding UTF8
        }
        Write-Host "Created Pico app at $project"
    }
    "build" {
        Invoke-PicoBuild -BuildDirectory $BuildDir
    }
    "flash" {
        Copy-PicoUf2 -BuildDirectory $BuildDir -Label $BoardLabel
    }
    "deploy" {
        Invoke-PicoDeploy -BuildDirectory $BuildDir -Label $BoardLabel
    }
    "list" {
        Get-Volume |
            Where-Object { $_.FileSystemLabel -eq $BoardLabel -and $_.DriveLetter } |
            Select-Object DriveLetter, FileSystemLabel, SizeRemaining, Size |
            Format-Table -AutoSize
    }
}
