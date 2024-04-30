@echo off

setlocal

pushd "%~dp0"

set ROOT_DIR=%~dp0
set RELEASE_DIR=%ROOT_DIR%_build\windows-x86_64\release
set PYTHON=%ROOT_DIR%_build\target-deps\python\python.exe

set PATH=%PATH%;%RELEASE_DIR%
set PYTHONPATH=%RELEASE_DIR%\python;%RELEASE_DIR%\bindings-python
set CARB_APP_PATH=%RELEASE_DIR%

if not exist "%PYTHON%" (
    echo Python, USD, and Omniverse Client libraries are missing.  Run "repo.bat build --stage" to retrieve them.
    popd
    exit /b
)

"%PYTHON%" "%RELEASE_DIR%/scripts/omni_asset_validator.py" %*

popd

EXIT /B %ERRORLEVEL%