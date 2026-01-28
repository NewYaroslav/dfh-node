@echo off
setlocal enabledelayedexpansion

rem Переход в корень репозитория (папка, где лежит этот батник)
cd /d "%~dp0"

if not exist build-msvc (
  if "%DFH_MSVC_GENERATOR%"=="" (
    cmake -S . -B build-msvc
    if errorlevel 1 goto :fail
  ) else (
    if "%DFH_MSVC_ARCH%"=="" set "DFH_MSVC_ARCH=x64"
    cmake -S . -B build-msvc -G "%DFH_MSVC_GENERATOR%" -A "%DFH_MSVC_ARCH%"
    if errorlevel 1 goto :fail
  )
)

echo [MSVC] Build Debug...
cmake --build build-msvc --config Debug
if errorlevel 1 goto :fail

echo.
echo [MSVC] Run ctest...
cd build-msvc
ctest -C Debug --output-on-failure
if errorlevel 1 goto :fail

cd /d "%~dp0"

echo.
echo [MinGW] Run build-tests-mingw.bat...
call build-tests-mingw.bat
if errorlevel 1 goto :fail

echo.
echo All tests (MSVC + MinGW) PASSED.
exit /b 0

:fail
echo.
echo Tests FAILED.
exit /b 1
