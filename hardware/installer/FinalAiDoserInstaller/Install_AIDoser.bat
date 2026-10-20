@echo off
cd /d "%~dp0"
where py >nul 2>nul
if %errorlevel%==0 (set "PY=py -3") else (set "PY=python")
%PY% --version >nul 2>nul
if errorlevel 1 (
 echo Python 3 is required. Install Python from https://www.python.org/downloads/windows/
 pause
 exit /b 1
)
if not exist ".aidoser_env\Scripts\python.exe" (
 echo Preparing AIDoser installer. Internet required for first setup...
 %PY% -m venv .aidoser_env
 if errorlevel 1 goto fail
 ".aidoser_env\Scripts\python.exe" -m pip install "esptool==4.8.1"
 if errorlevel 1 goto fail
)
".aidoser_env\Scripts\python.exe" AIDoser_Installer.py
if errorlevel 1 goto fail
exit /b 0
:fail
echo Installer could not start. Check Python and internet access.
pause
exit /b 1
