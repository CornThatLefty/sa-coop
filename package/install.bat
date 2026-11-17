@echo off
rem SA:COOP: installs the ASI loader when the game has none. Run it from the game folder.
cd /d "%~dp0"
rem the game's own files, not its exe: some builds name it otherwise (gta-sa.exe), issue #54
if not exist models\gta3.img (
    echo No game files here. The SA-COOP archive goes into the game folder, and install.bat runs from there.
    pause
    exit /b 1
)
if not exist coop.asi (
    echo coop.asi is missing. The whole SA-COOP archive goes into the game folder.
    pause
    exit /b 1
)
rem The game's own vorbisFile.dll has its name inside in lower case ("vorbisfile.dll"), an ASI loader in its place has not.
rem Ultimate ASI Loader (Widescreen Fix brings it as vorbisFile.dll) needs no vorbisHooked.dll: install.bat took it for the
rem game's library and renamed it, and under that name it closed the game ("This library isn't supported", 28.10).
if exist vorbisHooked.dll (
    findstr /m /c:"vorbisfile.dll" vorbisHooked.dll >nul
    if errorlevel 1 (
        findstr /m /c:"vorbisHooked" vorbisFile.dll >nul
        if not errorlevel 1 goto restore
    )
    echo The ASI loader is installed already. Nothing to change.
    echo The game is ready to start.
    pause
    exit /b 0
)
if not exist vorbisFile.dll (
    echo No vorbisFile.dll in the game folder. This does not look like GTA San Andreas 1.0.
    pause
    exit /b 1
)
findstr /m /c:"vorbisfile.dll" vorbisFile.dll >nul
if errorlevel 1 (
    echo The game has an ASI loader already: vorbisFile.dll is not the original one, e.g. from Widescreen Fix.
    echo That loader loads SA:COOP itself, nothing to change. The game is ready to start.
    pause
    exit /b 0
)
ren vorbisFile.dll vorbisHooked.dll
copy /y "coop\loader\vorbisFile.dll" vorbisFile.dll >nul
if errorlevel 1 (
    ren vorbisHooked.dll vorbisFile.dll
    echo Could not copy the loader. A game in Program Files needs install.bat run as administrator.
    pause
    exit /b 1
)
echo ASI loader installed. The original vorbisFile.dll is kept as vorbisHooked.dll.
echo The game is ready to start.
pause
exit /b 0

:restore
rem vorbisHooked.dll is another loader the old install.bat renamed, and vorbisFile.dll is ours: that one comes back
del vorbisFile.dll
if exist vorbisFile.dll (
    echo Could not remove the SA-COOP loader. The game has to be closed before install.bat runs again. A game in
    echo Program Files needs install.bat run as administrator.
    pause
    exit /b 1
)
ren vorbisHooked.dll vorbisFile.dll
if not exist vorbisFile.dll (
    echo Could not rename vorbisHooked.dll to vorbisFile.dll. It needs renaming by hand.
    pause
    exit /b 1
)
echo The previous ASI loader ^(e.g. from Widescreen Fix^) is back in place: the old install.bat had renamed it
echo to vorbisHooked.dll, and the game closed with the error "This library isn't supported". It loads SA:COOP itself.
echo The game is ready to start.
pause
