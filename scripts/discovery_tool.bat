@echo off
setlocal enabledelayedexpansion
cd /d "%~dp0"

where python >nul 2>nul
if errorlevel 1 (
    echo Python not found in PATH. Install Python 3 from https://python.org and try again.
    pause
    exit /b 1
)

:menu
cls
echo ============================================
echo   RavLight - UDP Device Discovery
echo ============================================
echo   1. Quick discovery (local network, broadcast)
echo   2. Discovery on a different segment/network (CIDR)
echo   3. Discovery on specific IPs
echo   4. Highlight device (blink)
echo   5. Reset device (factory reset)
echo   6. Exit
echo ============================================
set /p choice="Choose an option [1-6]: "

if "%choice%"=="1" goto local
if "%choice%"=="2" goto range
if "%choice%"=="3" goto targets
if "%choice%"=="4" goto highlight
if "%choice%"=="5" goto reset
if "%choice%"=="6" goto end
goto menu

:local
echo.
python discovery_tool.py
goto done

:range
echo.
set /p cidr="Enter the CIDR range (e.g. 192.168.10.0/24): "
if "%cidr%"=="" goto range
python discovery_tool.py --range %cidr%
goto done

:targets
echo.
set /p ips="Enter comma-separated IPs (e.g. 192.168.10.42,192.168.10.43): "
if "%ips%"=="" goto targets
python discovery_tool.py --targets %ips%
goto done

:highlight
echo.
set /p ip="IP of the device to blink: "
if "%ip%"=="" goto highlight
python discovery_tool.py --highlight %ip%
goto done

:reset
echo.
set /p ip="IP of the device to reset (WARNING: factory reset): "
if "%ip%"=="" goto reset
set /p confirm="Confirm reset of %ip%? [y/N]: "
if /i not "%confirm%"=="y" goto menu
python discovery_tool.py --reset %ip%
goto done

:done
echo.
pause
goto menu

:end
endlocal
