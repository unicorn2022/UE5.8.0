@echo off

rem ## Unreal Engine script to setup and run the bundled dotnet
rem ## Copyright Epic Games, Inc. All Rights Reserved.
rem ##
rem ## This script is expecting to exist in the Engine/Build/BatchFiles directory.  It will not work correctly
rem ## if you copy it to a different location and run it.

setlocal

rem Change the CWD to /Engine
pushd "%~dp0..\.."

rem First, make sure the batch file exists in the folder we expect it to.  This is necessary in order to
rem verify that our relative path to the /Engine directory is correct
if not exist "Build\BatchFiles\RunDotnet.bat" goto Error_BatchFileInWrongLocation

rem Verify that dotnet is present
call "%~dp0GetDotnetPath.bat"
if %ERRORLEVEL% NEQ 0 goto Error_NoDotnetSDK

echo Running dotnet %*
dotnet %*
if %ERRORLEVEL% NEQ 0 set RUNDOTNET_EXITCODE=%ERRORLEVEL%

goto Exit

:Error_BatchFileInWrongLocation
echo.
echo RunDotnet ERROR: The batch file does not appear to be located in the /Engine/Build/BatchFiles directory.  This script must be run from within that directory.
echo.
set RUNDOTNET_EXITCODE=1
goto Exit

:Error_NoDotnetSDK
echo.
echo RunDotnet ERROR: Unable to find a install of Dotnet SDK.  Please make sure you have it installed and that `dotnet` is a globally available command.
echo.
set RUNDOTNET_EXITCODE=1
goto Exit

:Exit
rem ## Restore original CWD in case we change it
popd
exit /B %RUNDOTNET_EXITCODE%
