@echo off

pushd "%~dp0"
call _build\windows-x86_64\release\HelloWorld.exe %*
if errorlevel 1 ( echo Error running HelloWorld )
popd

EXIT /B %ERRORLEVEL%