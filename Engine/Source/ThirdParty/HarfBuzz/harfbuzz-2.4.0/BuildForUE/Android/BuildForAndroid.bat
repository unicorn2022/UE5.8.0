@ECHO OFF

REM Copyright Epic Games, Inc. All Rights Reserved.
REM
REM Builds HarfBuzz 2.4.0 static libraries for Android (ARM64, x64).
REM CMakeLists.txt compiles against ICU 78 and FreeType2-2.14.1 headers.

setlocal

set ENGINE_ROOT=%~dp0..\..\..\..\..\..\
set HB_VERSION=harfbuzz-2.4.0
set MAKE_TARGET=harfbuzz

if not exist "%ANDROID_NDK_ROOT%\source.properties" (
    echo Please set ANDROID_NDK_ROOT to the Android NDK directory!
    exit /b 1
)

set COMMON_ARGS=-TargetLib=HarfBuzz -TargetLibVersion=%HB_VERSION% -TargetConfigs=Debug+Release -LibOutputPath=lib-icu78 -CMakeGenerator=Makefile -MakeTarget=%MAKE_TARGET% -SkipCreateChangelist
set ANDROID_CMAKE_ARGS=-DUSE_INTEL_ATOMIC_PRIMITIVES=TRUE

echo === Building HarfBuzz for Android ARM64 (ICU 78) ===
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Android -TargetArchitecture=ARM64 %COMMON_ARGS% -CMakeAdditionalArguments="%ANDROID_CMAKE_ARGS%" || exit /b

echo === Building HarfBuzz for Android x64 (ICU 78) ===
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Android -TargetArchitecture=x64 %COMMON_ARGS% -CMakeAdditionalArguments="%ANDROID_CMAKE_ARGS%" || exit /b

echo Done. Libraries output to:
echo   lib-icu78/Android/ARM64/{Debug,Release}/libharfbuzz.a
echo   lib-icu78/Android/x64/{Debug,Release}/libharfbuzz.a

endlocal
