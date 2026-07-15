@ECHO OFF

REM Copyright Epic Games, Inc. All Rights Reserved.
REM
REM Builds ICU 78 static libraries for Linux (x86_64-unknown-linux-gnu, aarch64-unknown-linux-gnueabi).

setlocal

set ENGINE_ROOT=%CD%\..\..\..\..\..\..\

set ICU_VERSION=icu4c-78_1
set COMMON_ARGS=-TargetLib=ICU -TargetLibVersion=%ICU_VERSION% -TargetConfigs=Debug+Release -LibOutputPath=lib -CMakeGenerator=Makefile -MakeTarget=icu -SkipCreateChangelist -CMakeAdditionalArguments="-DBUILD_WITH_LIBCXX=ON"

echo Creating ICU 78 libraries for x86_64-unknown-linux-gnu...
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Unix -TargetArchitecture=x86_64-unknown-linux-gnu %COMMON_ARGS% || exit /b

echo Creating ICU 78 libraries for aarch64-unknown-linux-gnueabi...
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Unix -TargetArchitecture=aarch64-unknown-linux-gnueabi %COMMON_ARGS% || exit /b

endlocal
