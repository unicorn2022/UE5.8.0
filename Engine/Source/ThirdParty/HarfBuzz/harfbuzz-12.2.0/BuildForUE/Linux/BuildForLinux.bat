@ECHO OFF

REM Copyright Epic Games, Inc. All Rights Reserved.
REM
REM Builds HarfBuzz 12.2.0 static libraries for Linux
REM (x86_64-unknown-linux-gnu, aarch64-unknown-linux-gnueabi)
REM against ICU 78 and FreeType2-2.14.1.

setlocal

set ENGINE_ROOT=%~dp0..\..\..\..\..\..\
set HB_VERSION=harfbuzz-12.2.0
set MAKE_TARGET=harfbuzz

set COMMON_ARGS=-TargetLib=HarfBuzz -TargetLibVersion=%HB_VERSION% -TargetConfigs=Debug+Release -LibOutputPath=lib -CMakeGenerator=Makefile -MakeTarget=%MAKE_TARGET% -SkipCreateChangelist -CMakeAdditionalArguments="-DBUILD_WITH_LIBCXX=ON"

echo === Building HarfBuzz 12.2.0 for x86_64-unknown-linux-gnu ===
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Unix -TargetArchitecture=x86_64-unknown-linux-gnu %COMMON_ARGS% || exit /b

echo === Building HarfBuzz 12.2.0 for aarch64-unknown-linux-gnueabi ===
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Unix -TargetArchitecture=aarch64-unknown-linux-gnueabi %COMMON_ARGS% || exit /b

echo Done. Libraries output to:
echo   lib/Unix/x86_64-unknown-linux-gnu/{Debug,Release}/libharfbuzz.a
echo   lib/Unix/aarch64-unknown-linux-gnueabi/{Debug,Release}/libharfbuzz.a

endlocal
