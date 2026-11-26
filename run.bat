@echo off
cd /d "%~dp0"
if exist "release\FoxSgfsDownload-Max.exe" (
    start "" "release\FoxSgfsDownload-Max.exe"
) else if exist "build\Release\FoxSgfsDownload-Max.exe" (
    start "" "build\Release\FoxSgfsDownload-Max.exe"
) else (
    echo 未找到 FoxSgfsDownload-Max.exe，请先运行 build.bat。
    pause
)

