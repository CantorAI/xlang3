@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build build-repro/main-verify-20261006 --config Release > doc/performance/data/dict-scalar-append-runtime-index-build-20261008.log 2>&1
exit /b %errorlevel%
