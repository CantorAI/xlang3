@echo off
cd /d D:\CantorAI\xlang3
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
if not exist scratch\performance\sqlalchemy-direct-native-parser-20261008 mkdir scratch\performance\sqlalchemy-direct-native-parser-20261008
if errorlevel 1 exit /b %errorlevel%
cl /nologo /EHsc /O2 /Ob3 /DNDEBUG /std:c++17 /MD /DXLANG3_USE_SHARED /I src /I src\internal /I tests\cpp /I third_party\nlohmann_json /I sdk /Fo"scratch\performance\sqlalchemy-direct-native-parser-20261008\direct-parser.obj" /Fe"scratch\performance\sqlalchemy-direct-native-parser-20261008\direct-parser.exe" scratch\performance\sqlalchemy-direct-native-parser-proposed-20261008.cpp build-repro\main-verify-20261006\Release\xlang3_runtime.lib
exit /b %errorlevel%
