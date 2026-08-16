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

Build prerequisites:

```powershell
git clone https://github.com/raspberrypi/pico-sdk D:\pico\pico-sdk
$env:PICO_SDK_PATH = "D:\pico\pico-sdk"
cmake -S D:\CantorAI\xlang3\ports\rp2040 -B D:\CantorAI\xlang3\build-rp2040 -G Ninja
cmake --build D:\CantorAI\xlang3\build-rp2040
```

Or build and flash a Pico mounted as `RPI-RP2`:

```powershell
D:\CantorAI\xlang3\ports\rp2040\build_and_flash.ps1
```

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

The first target is only to prove that the parser, sema, ProgramIR, interpreter, `X3Value`, refcounted objects, and core builtins can fit into an RP2040 firmware. After that, the runtime should get a smaller embedded profile with fixed allocators, optional parser exclusion, and prebuilt IR blobs.
