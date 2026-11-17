@echo off
cd /d "%~dp0"
if exist "release\FoxKifuStudio.exe" (
    start "" "release\FoxKifuStudio.exe"
) else if exist "build\Release\FoxKifuStudio.exe" (
    start "" "build\Release\FoxKifuStudio.exe"
) else (
    echo 未找到 FoxKifuStudio.exe，请先运行 build.bat。
    pause
)
