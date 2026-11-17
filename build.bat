@echo off
setlocal
cd /d "%~dp0"

echo [1/2] Configuring CMake...
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
if errorlevel 1 (
    echo Visual Studio 18 not found, trying Visual Studio 17...
    cmake -S . -B build -G "Visual Studio 17 2022" -A x64
)
if errorlevel 1 goto fail

echo [2/2] Building Release...
cmake --build build --config Release
if errorlevel 1 goto fail

if not exist release mkdir release
copy /Y "build\Release\FoxKifuStudio.exe" "release\FoxKifuStudio.exe" >nul
copy /Y "root.txt" "release\root.txt" >nul
echo.
echo Build OK: %~dp0release\FoxKifuStudio.exe
goto end

:fail
echo.
echo Build FAILED.
exit /b 1

:end
endlocal
