@echo off
REM ============================================================================
REM Downloader unified deploy / portable / installer script
REM
REM Does three things:
REM   1) windeployqt the freshly-built build\Downloader.exe into installer\temp\
REM   2) Produce Downloader-portable.zip (extract-and-run) from installer\temp\
REM   3) Pack installer\packages\com.downloader.app\data\app.7z from installer\temp\
REM      then run binarycreator to produce installer\output\DownloaderInstaller.exe
REM
REM Usage:
REM   tools\deploy.bat                (deploy only, refresh installer\temp\)
REM   tools\deploy.bat portable       (deploy + portable zip)
REM   tools\deploy.bat installer      (deploy + installer)
REM   tools\deploy.bat all           (deploy + both; this is the default)
REM
REM Exit codes: 0 on success, non-zero on first failure.
REM ============================================================================
setlocal enabledelayedexpansion

REM All paths are resolved relative to this script so the package is portable
set "ROOT=%~dp0.."
pushd "%ROOT%"

set "BUILD=%ROOT%\build"
set "DEPLOY_DIR=%ROOT%\installer\temp"
set "PORTABLE_OUT=%ROOT%\installer\portable\Downloader-portable.zip"
set "PACKAGE_DATA=%ROOT%\installer\packages\com.downloader.app\data\app.7z"
set "INSTALLER_OUT=%ROOT%\installer\output\DownloaderInstaller.exe"

set "MODE=%~1"
if "%MODE%"=="" set "MODE=all"

REM Qt / IFW path discovery - prefer env vars, fall back to project defaults
REM Keep these defaults in sync with CMakeLists.txt:3 and existing installer\build.bat
if "%Qt6_DIR%"=="" set "Qt6_DIR=D:/Qt/6.11.1/mingw_64"
if "%IFW_PATH%"=="" set "IFW_PATH=D:\Qt\Tools\QtInstallerFramework\4.10\bin"
set "WINDEPLOYQT=%Qt6_DIR%\bin\windeployqt.exe"
set "BINARYCREATOR=%IFW_PATH%\binarycreator.exe"
REM 7-Zip is preferred for the installer payload (smaller + LZMA)
REM Default to /c/Program Files/7-Zip/, then chocolatey. Override with SEVENZIP env var.
if "%SEVENZIP%"=="" set "SEVENZIP=C:\Program Files\7-Zip\7z.exe"
if not exist "%SEVENZIP%" set "SEVENZIP=C:\ProgramData\chocolatey\tools\7z.exe"

echo ===== Downloader deploy pipeline =====
echo Qt  : %Qt6_DIR%
echo IFW : %IFW_PATH%
echo Mode: %MODE%
echo.

REM ---- Step 1: build (incremental cmake build; skip if up-to-date exe exists) ----
if not exist "%BUILD%\Downloader.exe" (
    echo [1/4] No %BUILD%\Downloader.exe found, building...
    if not exist "%BUILD%\CMakeCache.txt" (
        if not exist "%BUILD%" mkdir "%BUILD%"
        cmake -G "MinGW Makefiles" -S "%ROOT%" -B "%BUILD%" || goto :err
    )
    cmake --build "%BUILD%" -j 4 --target Downloader || goto :err
) else (
    echo [1/4] build\Downloader.exe already present, skipping build
)
echo.

REM ---- Step 2: windeployqt ----
echo [2/4] Refreshing %DEPLOY_DIR% via windeployqt ...
if exist "%DEPLOY_DIR%" rmdir /s /q "%DEPLOY_DIR%"
mkdir "%DEPLOY_DIR%"
copy /y "%BUILD%\Downloader.exe" "%DEPLOY_DIR%\" >nul
"%WINDEPLOYQT%" --release --no-translations --no-system-d3d-compiler --no-opengl-sw "%DEPLOY_DIR%\Downloader.exe" || goto :err
REM windeployqt drops its own translations set above; ship the two we ship in-app too
if exist "%Qt6_DIR%\translations\qt_zh_CN.qm" copy /y "%Qt6_DIR%\translations\qt_zh_CN.qm" "%DEPLOY_DIR%\translations\" >nul 2>&1
if exist "%Qt6_DIR%\translations\qt_en_US.qm" copy /y "%Qt6_DIR%\translations\qt_en_US.qm" "%DEPLOY_DIR%\translations\" >nul 2>&1
REM Copy portable-only helpers (start.bat + README) so they ship inside the zip.
REM They live under installer\portable\ (not installer\temp\) because deploy
REM wipes temp before windeployqt runs.
if exist "%ROOT%\installer\portable\start.bat" copy /y "%ROOT%\installer\portable\start.bat" "%DEPLOY_DIR%\start.bat" >nul 2>&1
if exist "%ROOT%\installer\portable\README.txt" copy /y "%ROOT%\installer\portable\README.txt" "%DEPLOY_DIR%\README.txt" >nul 2>&1
echo     OK
echo.

REM ---- Step 3: portable zip ----
if /i "%MODE%"=="portable" goto :do_portable_only
if /i "%MODE%"=="all" goto :do_portable
goto :skip_portable

:do_portable_only
echo [3/4] Building portable archive ...
goto :do_portable

:do_portable
echo [3/4] Building portable archive -^> %PORTABLE_OUT%
if not exist "%ROOT%\installer\portable" mkdir "%ROOT%\installer\portable"
REM PowerShell Compress-Archive is built into Windows 10+ so this works without
REM any extra tool installed on the build machine.
powershell -NoProfile -Command "Compress-Archive -Path '%DEPLOY_DIR%\*' -DestinationPath '%PORTABLE_OUT%' -Force"
if errorlevel 1 goto :err
echo     OK
echo.

:skip_portable

REM ---- Step 4: installer 7z + binarycreator ----
if /i "%MODE%"=="installer" goto :do_installer_only
if /i "%MODE%"=="all" goto :do_installer
goto :skip_installer

:do_installer_only
echo [4/4] Building installer ...
goto :do_installer

:do_installer
echo [4/4] Building installer ...

REM 4a: pack the deployed app into a 7z payload
if not exist "%ROOT%\installer\packages\com.downloader.app\data" mkdir "%ROOT%\installer\packages\com.downloader.app\data"
if exist "%PACKAGE_DATA%" del /f /q "%PACKAGE_DATA%"
"%SEVENZIP%" a -t7z -mx=9 "%PACKAGE_DATA%" "%DEPLOY_DIR%\*" || goto :err
echo     payload: %PACKAGE_DATA%

REM 4b: run binarycreator to assemble the offline installer.
REM Quirky: invoking binarycreator.exe through cmd.exe silently no-ops (exit 0,
REM no output file). Calling it through Git Bash works, so we delegate step 4b
REM to a tiny shell wrapper (tools\run_binarycreator.sh).
if not exist "%ROOT%\installer\output" mkdir "%ROOT%\installer\output"
if exist "%INSTALLER_OUT%" del /f /q "%INSTALLER_OUT%"
set "BASH=%ProgramFiles%\Git\bin\bash.exe"
if not exist "%BASH%" set "BASH=C:\Program Files\Git\bin\bash.exe"
"%BASH%" "%ROOT%\tools\run_binarycreator.sh" "%BINARYCREATOR%" "%ROOT%\installer\config.xml" "%ROOT%\installer\packages" "%INSTALLER_OUT%"
if errorlevel 1 goto :err
if not exist "%INSTALLER_OUT%" (
    echo !!!! binarycreator returned but %INSTALLER_OUT% was not created !!!!
    goto :err
)
echo     installer: %INSTALLER_OUT%
echo.

:skip_installer

echo ===== Done =====
echo deploy root : %DEPLOY_DIR%
if exist "%PORTABLE_OUT%" echo portable  : %PORTABLE_OUT%
if exist "%INSTALLER_OUT%" echo installer : %INSTALLER_OUT%
goto :end

:err
echo.
echo !!!! pipeline failed, exit code %ERRORLEVEL% !!!!
popd
endlocal
exit /b %ERRORLEVEL%

:end
popd
endlocal
exit /b 0
