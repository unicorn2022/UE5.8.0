@ECHO OFF

REM Copyright Epic Games, Inc. All Rights Reserved.
REM
REM Builds HarfBuzz 2.4.0 static libraries for Windows (MSVC x64, MSVC ARM64).
REM CMakeLists.txt compiles against ICU 78 and FreeType2-2.14.1 headers.

setlocal

set ENGINE_ROOT=%~dp0..\..\..\..\..\..\
set HB_VERSION=harfbuzz-2.4.0
set MAKE_TARGET=harfbuzz

echo === Building HarfBuzz for MSVC x64 (ICU 78) ===
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetLib=HarfBuzz -TargetLibVersion=%HB_VERSION% -TargetConfigs=Debug+Release -LibOutputPath=lib-icu78 -TargetArchitecture=x64 -CMakeGenerator=VS2022 -MakeTarget=%MAKE_TARGET% -SkipCreateChangelist || exit /b


echo === Building HarfBuzz for MSVC ARM64 ===
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetLib=HarfBuzz -TargetLibVersion=%HB_VERSION% -TargetConfigs=Debug+Release -LibOutputPath=lib-icu78 -TargetArchitecture=ARM64 -CMakeGenerator=VS2022 -MakeTarget=%MAKE_TARGET% -SkipCreateChangelist || exit /b

echo Done. Libraries output to:
echo   lib-icu78/Win64/x64/{Debug,Release}/harfbuzz.lib
echo   lib-icu78/Win64/arm64/{Debug,Release}/harfbuzz.lib

endlocal
