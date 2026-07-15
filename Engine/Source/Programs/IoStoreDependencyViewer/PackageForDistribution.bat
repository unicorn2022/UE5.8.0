@echo off
REM IoStoreDependencyViewer Distribution Packaging Script

echo.
echo ========================================
echo  IoStoreDependencyViewer Packager
echo ========================================
echo.
echo Reading settings from PackageSettings.ini...
echo.

REM Run the PowerShell script
powershell.exe -ExecutionPolicy Bypass -File "%~dp0PackageForDistribution.ps1"

if %ERRORLEVEL% EQU 0 (
    echo.
    echo SUCCESS!
    pause
) else (
    echo.
    echo FAILED! Check the error messages above.
    pause
    exit /b 1
)
