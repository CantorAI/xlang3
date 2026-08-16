@echo off
rem Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
rem Licensed under the Apache License, Version 2.0 (the "License");
rem you may not use this file except in compliance with the License.
rem You may obtain a copy of the License at
rem
rem     http://www.apache.org/licenses/LICENSE-2.0
rem
rem Unless required by applicable law or agreed to in writing, software
rem distributed under the License is distributed on an "AS IS" BASIS,
rem WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
rem See the License for the specific language governing permissions and
rem limitations under the License.

setlocal

if not defined PICO_SDK_PATH set "PICO_SDK_PATH=D:\pico\pico-sdk"
set "XLANG3_ARM_GCC=%APPDATA%\xPacks\@xpack-dev-tools\arm-none-eabi-gcc\15.2.1-1.1.1\.content\bin"
set "PATH=%XLANG3_ARM_GCC%;%PATH%"

call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b %errorlevel%

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build_and_flash.ps1" -BuildDir D:\CantorAI\xlang3\build-rp2040-vs
exit /b %errorlevel%
