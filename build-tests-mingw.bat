@echo off
set BUILD_DIR=build-tests-mingw

if not exist %BUILD_DIR% mkdir %BUILD_DIR%
if errorlevel 1 goto :fail

cmake -S . -B %BUILD_DIR% -G "MinGW Makefiles" -DDFH_NODE_BUILD_TESTS=ON
if errorlevel 1 goto :fail

cmake --build %BUILD_DIR%
if errorlevel 1 goto :fail

cd %BUILD_DIR%
ctest --output-on-failure
if errorlevel 1 goto :fail

pause
exit /b 0

:fail
echo Tests FAILED.
pause
exit /b 1
