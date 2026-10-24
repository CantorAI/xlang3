@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /O2 /EHsc /MD /D XLANG3_USE_SHARED /I src\internal /I sdk scratch\performance\dict-scalar-append-freshness-diagnostic-r2-root-20261008.cpp /Fe:scratch\performance\dict-scalar-append-freshness-diagnostic-r2-root-20261008.exe /Fo:scratch\performance\dict-scalar-append-freshness-diagnostic-r2-root-20261008.obj /link build-repro\main-verify-20261006\Release\xlang3_runtime.lib
exit /b %errorlevel%
