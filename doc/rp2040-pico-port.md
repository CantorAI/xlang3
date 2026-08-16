<!--
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
-->
# RP2040 Pico Port

Status: first embedded build slice.

Target:

- Raspberry Pi Pico / RP2040
- Cortex-M0+
- static firmware image
- no dynamic native packages
- no source file import path
- source text embedded in firmware for the first bring-up

Current port layout:

```text
ports/rp2040/
  main.cpp
  apps/main.py
  board/
    board_config.h
    console.cpp/.h
    gpio.cpp/.h
    time.cpp/.h
  embedded/
    embedded_host.cpp/.h
    frozen_app.cpp/.h
    static_modules.cpp/.h
```

`main.cpp` is board startup only. Hardware access belongs under `board/`.
The embedded host owns the execution-facing shape: source loading, static module
registration, and later parser/IR/executor invocation.

Build prerequisites:

```powershell
git clone https://github.com/raspberrypi/pico-sdk D:\pico\pico-sdk
$env:PICO_SDK_PATH = "D:\pico\pico-sdk"
cmake -S D:\CantorAI\xlang3\ports\rp2040 -B D:\CantorAI\xlang3\build-rp2040 -G Ninja
cmake --build D:\CantorAI\xlang3\build-rp2040
```

Current Windows setup used for bring-up:

```text
Pico SDK: D:\pico\pico-sdk
ARM GCC:  %APPDATA%\xPacks\@xpack-dev-tools\arm-none-eabi-gcc\15.2.1-1.1.1\.content\bin
```

Or build and flash a Pico mounted as `RPI-RP2`:

```powershell
D:\CantorAI\xlang3\ports\rp2040\build_and_flash.ps1
```

On Windows without an already-open Visual Studio developer shell:

```cmd
D:\CantorAI\xlang3\ports\rp2040\build_and_flash_vs.cmd
```

The host-side Pico CLI is:

```powershell
D:\CantorAI\xlang3\tools\xlang3-pico\xlang3-pico.ps1 build
D:\CantorAI\xlang3\tools\xlang3-pico\xlang3-pico.ps1 flash
D:\CantorAI\xlang3\tools\xlang3-pico\xlang3-pico.ps1 deploy
```

`build` only builds the UF2. `flash` only copies the existing UF2 to a Pico
mounted as `RPI-RP2`. `deploy` does both. Interactive work belongs to the
normal `xlang3` CLI and the `device` package, not to the flashing helper.

Expected output:

```text
build-rp2040\xlang3_pico.uf2
```

Current embedded runtime excludes:

- desktop CLI
- `x3_runtime_eval_file`
- `.py` source file imports
- dynamic native package loading
- JSON/YAML/SQLite packages
- filesystem-based debug IR output

Static native modules are the embedded equivalent of desktop native packages.
The current registry reserves:

```text
gpio
time
console
```

These names should later become normal imports from Pico Python code:

```python
import gpio
import time

led = gpio.Pin(15, gpio.OUT)
```

The first target is only to prove that the parser, sema, ProgramIR, interpreter,
`X3Value`, refcounted objects, and core builtins can fit into an RP2040
firmware. After that, the runtime should get a smaller embedded profile with
fixed allocators, optional parser exclusion, and prebuilt IR blobs.
