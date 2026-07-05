@echo off
REM ============================================================================
REM Downloader portable launcher
REM
REM Pins the working directory to the script's own location, so launching
REM from a shortcut / "Run as administrator" / context menu still finds
REM the DLLs sitting next to Downloader.exe.
REM ============================================================================
setlocal
cd /d "%~dp0"
start "" "%~dp0Downloader.exe"
endlocal
