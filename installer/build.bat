@echo off
setlocal

REM Qt Installer Framework 路径：优先用环境变量 IFW_PATH，否则用默认路径
set IFW=%IFW_PATH%
if "%IFW%"=="" set IFW=C:\Qt\Tools\QtInstallerFramework\4.6\bin

if not exist output mkdir output
if not exist packages (
    echo packages directory not found
    exit /b 1
)

if not exist "%IFW%\binarycreator.exe" (
    echo binarycreator.exe not found at "%IFW%"
    echo Please set IFW_PATH environment variable to the IFW bin directory.
    exit /b 1
)

"%IFW%\binarycreator.exe" --offline-only -c config.xml -p packages output\DownloaderInstaller.exe
exit /b %ERRORLEVEL%