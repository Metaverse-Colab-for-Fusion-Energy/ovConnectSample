@echo off

pushd "%~dp0"
call _build\windows-x86_64\release\LiveSession.exe %*
if errorlevel 1 ( echo Error running LiveSession )
popd

EXIT /B %ERRORLEVEL%