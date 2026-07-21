@echo off
cd /d "%~dp0"

set PYW=pythonw
where pythonw >nul 2>nul
if errorlevel 1 (
    if exist "%LOCALAPPDATA%\Programs\Python\Python312\pythonw.exe" (
        set PYW="%LOCALAPPDATA%\Programs\Python\Python312\pythonw.exe"
    ) else if exist "%LOCALAPPDATA%\Programs\Python\Python313\pythonw.exe" (
        set PYW="%LOCALAPPDATA%\Programs\Python\Python313\pythonw.exe"
    ) else if exist "%LOCALAPPDATA%\Programs\Python\Python311\pythonw.exe" (
        set PYW="%LOCALAPPDATA%\Programs\Python\Python311\pythonw.exe"
    ) else (
        echo Python not found. Install Python 3 from https://python.org
        echo ^(make sure to check "Add python.exe to PATH" during setup^)
        pause
        exit /b 1
    )
)

start "" %PYW% ravlight_companion.py
