@echo off

setlocal

set ROOT_DIR=%~dp0..
pushd "%ROOT_DIR%"

set PYTHON=%ROOT_DIR%\_build\target-deps\python\python.exe
set PYTHONPATH=%ROOT_DIR%\source\pyHelloWorld

if not exist "%PYTHON%" (
    echo Python, USD, and Omniverse Client libraries are missing.  Run "repo.bat build --stage" to retrieve them.
    popd
    exit /b
)

"%PYTHON%" tests\test_all.py %*

popd

EXIT /B %ERRORLEVEL%