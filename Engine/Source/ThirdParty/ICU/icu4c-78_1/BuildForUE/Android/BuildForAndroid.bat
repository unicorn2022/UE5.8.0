@ECHO OFF

REM Copyright Epic Games, Inc. All Rights Reserved.
REM
REM Builds ICU 78 static libraries for Android (ARM64, x64).

setlocal

set ENGINE_ROOT=%~dp0..\..\..\..\..\..

if not exist "%ANDROID_NDK_ROOT%\source.properties" (
    echo Please set ANDROID_NDK_ROOT to the Android NDK directory!
    exit /b 1
)

set ICU_VERSION=icu4c-78_1
set COMMON_ARGS=-TargetLib=ICU -TargetLibVersion=%ICU_VERSION% -TargetConfigs=Debug+Release -LibOutputPath=lib -CMakeGenerator=Makefile -MakeTarget=icu -SkipCreateChangelist

call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Android -TargetArchitecture=ARM64 %COMMON_ARGS% || exit /b
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Android -TargetArchitecture=x64   %COMMON_ARGS% || exit /b
