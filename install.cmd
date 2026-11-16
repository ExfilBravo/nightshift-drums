@echo off
rem Puts the built Nightshift Drums where it's used, for this user (no admin rights):
rem   the VST3 in %LOCALAPPDATA%\Programs\Common\VST3 (where NightShift and FL Studio find it, beside the Lofi and Bass plugins)
rem   the standalone app in %LOCALAPPDATA%\Programs\Nightshift Drums, with a Start menu shortcut
setlocal
set "SRC=%~dp0build\Release\NightshiftDrums_artefacts\Release"
set "DEST=%LOCALAPPDATA%\Programs\Nightshift Drums"
set "VST3=%LOCALAPPDATA%\Programs\Common\VST3"

if not exist "%SRC%\VST3\Nightshift Drums.vst3" (echo Build it first: build.cmd Release & exit /b 1)

if not exist "%VST3%" mkdir "%VST3%"
rem A host with the plugin open holds its binary: it can't be overwritten, but it can be renamed aside.
rem The host keeps the copy it has open; the next time it starts, it loads this one.
set "BIN=%VST3%\Nightshift Drums.vst3\Contents\x86_64-win\Nightshift Drums.vst3"
del "%BIN%.old-*" >nul 2>&1
del "%BIN%" >nul 2>&1
if exist "%BIN%" (
    move /y "%BIN%" "%BIN%.old-%RANDOM%" >nul 2>&1
    echo A host has Nightshift Drums open: it keeps the one it has until it starts again.
)
robocopy "%SRC%\VST3\Nightshift Drums.vst3" "%VST3%\Nightshift Drums.vst3" /E /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (echo Couldn't copy the VST3 - is a host using it? & exit /b 1)

if not exist "%DEST%" mkdir "%DEST%"
rem A running copy can't be overwritten, but it can be renamed aside
if exist "%DEST%\Nightshift Drums.exe" (
    del "%DEST%\Nightshift Drums.old.exe" >nul 2>&1
    move /y "%DEST%\Nightshift Drums.exe" "%DEST%\Nightshift Drums.old.exe" >nul 2>&1
)
copy /y "%SRC%\Standalone\Nightshift Drums.exe" "%DEST%\Nightshift Drums.exe" >nul || (echo Couldn't copy the app & exit /b 1)

powershell -NoProfile -Command "$s = (New-Object -ComObject WScript.Shell).CreateShortcut([Environment]::GetFolderPath('Programs') + '\Nightshift Drums.lnk'); $s.TargetPath = '%DEST%\Nightshift Drums.exe'; $s.WorkingDirectory = '%DEST%'; $s.Description = 'Nightshift Drums'; $s.Save()" || (echo Couldn't make the Start menu shortcut & exit /b 1)

echo Installed: the VST3 in %VST3%, the app in %DEST% (Start menu: Nightshift Drums)
