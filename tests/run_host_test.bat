@echo off
rem Standalone host test for the pure compiler core (no engine, no UBT).
rem Run from the plugin root: tests\run_host_test.bat
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cl /nologo /std:c++17 /utf-8 /EHsc /W3 /I Source\LeoNarrative\Public ^
  tests\host_test.cpp ^
  Source\LeoNarrative\Private\Script\LeoCompiler.cpp ^
  Source\LeoNarrative\Private\Script\LeoExpr.cpp ^
  Source\LeoNarrative\Private\Script\LeoTypes.cpp ^
  /Fe:tests\host_test.exe /Fo:tests\ >nul
if errorlevel 1 (
  echo [FAIL] host test compile error
  exit /b 1
)
tests\host_test.exe
exit /b %errorlevel%
