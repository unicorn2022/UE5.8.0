@ECHO OFF

REM Copyright Epic Games, Inc. All Rights Reserved.
REM
REM Builds HarfBuzz 2.4.0 static libraries for Linux (x86_64-unknown-linux-gnu, aarch64-unknown-linux-gnueabi).
REM CMakeLists.txt compiles against ICU 78 and FreeType2-2.14.1 headers.

setlocal

set ENGINE_ROOT=%~dp0..\..\..\..\..\..\
set HB_VERSION=harfbuzz-2.4.0
set MAKE_TARGET=harfbuzz

if not exist "%LINUX_MULTIARCH_ROOT%" (
    echo Please set LINUX_MULTIARCH_ROOT to the Linux toolchain directory!
    exit /b 1
)

set COMMON_ARGS=-TargetLib=HarfBuzz -TargetLibVersion=%HB_VERSION% -TargetConfigs=Debug+Release -LibOutputPath=lib-icu78 -CMakeGenerator=Makefile -MakeTarget=%MAKE_TARGET% -SkipCreateChangelist -CMakeAdditionalArguments="-DBUILD_WITH_LIBCXX=ON"

echo === Building HarfBuzz for Linux x86_64-unknown-linux-gnu (ICU 78) ===
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Unix -TargetArchitecture=x86_64-unknown-linux-gnu %COMMON_ARGS% || exit /b

echo === Building HarfBuzz for Linux aarch64-unknown-linux-gnueabi (ICU 78) ===
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Unix -TargetArchitecture=aarch64-unknown-linux-gnueabi %COMMON_ARGS% || exit /b

echo Done. Libraries output to:
echo   lib-icu78/Unix/x86_64-unknown-linux-gnu/{Debug,Release}/libharfbuzz.a
echo   lib-icu78/Unix/aarch64-unknown-linux-gnueabi/{Debug,Release}/libharfbuzz.a

endlocal
