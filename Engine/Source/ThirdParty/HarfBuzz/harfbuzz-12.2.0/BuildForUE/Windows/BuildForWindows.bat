@ECHO OFF

REM Copyright Epic Games, Inc. All Rights Reserved.
REM
REM Builds HarfBuzz 12.2.0 static libraries for Windows (MSVC x64, MSVC ARM64)
REM against ICU 78 and FreeType2-2.14.1.

setlocal

set ENGINE_ROOT=%~dp0..\..\..\..\..\..\
set HB_VERSION=harfbuzz-12.2.0
set MAKE_TARGET=harfbuzz

echo === Building HarfBuzz 12.2.0 for MSVC x64 ===
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetLib=HarfBuzz -TargetLibVersion=%HB_VERSION% -TargetConfigs=Debug+Release -LibOutputPath=lib -TargetArchitecture=x64 -CMakeGenerator=VS2022 -MakeTarget=%MAKE_TARGET% -SkipCreateChangelist || exit /b

echo === Building HarfBuzz 12.2.0 for MSVC ARM64 ===
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetLib=HarfBuzz -TargetLibVersion=%HB_VERSION% -TargetConfigs=Debug+Release -LibOutputPath=lib -TargetArchitecture=ARM64 -CMakeGenerator=VS2022 -MakeTarget=%MAKE_TARGET% -SkipCreateChangelist || exit /b

echo Done. Libraries output to:
echo   lib/Win64/x64/{Debug,Release}/harfbuzz.lib
echo   lib/Win64/arm64/{Debug,Release}/harfbuzz.lib

endlocal
