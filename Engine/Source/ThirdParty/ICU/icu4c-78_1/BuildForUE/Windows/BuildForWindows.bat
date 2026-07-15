@ECHO OFF

REM Copyright Epic Games, Inc. All Rights Reserved.
REM
REM Builds ICU 78 static libraries for Windows (MSVC x64, MSVC ARM64, Clang x64).
REM

setlocal

set ENGINE_ROOT=%~dp0..\..\..\..\..\..\
set ICU_VERSION=icu4c-78_1
set MAKE_TARGET=icu

REM === MSVC x64 (via BuildCMakeLib) ===
echo Building ICU for MSVC x64...
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetLib=ICU -TargetLibVersion=%ICU_VERSION% -TargetConfigs=Debug+Release -LibOutputPath=lib -TargetArchitecture=x64 -CMakeGenerator=VS2022 -MakeTarget=%MAKE_TARGET% -SkipCreateChangelist || exit /b

REM === MSVC ARM64 (via BuildCMakeLib) ===
echo Building ICU for MSVC ARM64...
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetLib=ICU -TargetLibVersion=%ICU_VERSION% -TargetConfigs=Debug+Release -LibOutputPath=lib -TargetArchitecture=ARM64 -CMakeGenerator=VS2022 -MakeTarget=%MAKE_TARGET% -SkipCreateChangelist || exit /b

REM === Clang x64 (manual cmake — BuildCMakeLib does not support ClangCL toolset) ===
set PATH_TO_CMAKE_FILE=%~dp0..
set CLANG_BUILD_PATH=%PATH_TO_CMAKE_FILE%\..\lib\Win64\Clang\Build
set CLANG_OUTPUT_PATH=%PATH_TO_CMAKE_FILE%\..\lib\Win64\Clang

REM Find MSBuild via vswhere
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist %VSWHERE% (
	echo ERROR: vswhere.exe not found. Is Visual Studio installed?
	exit /b 1
)

for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe`) do set MSBUILD_EXE=%%i
if not defined MSBUILD_EXE (
	echo ERROR: Could not find MSBuild via vswhere.
	exit /b 1
)

if exist "%CLANG_BUILD_PATH%" (
	rmdir "%CLANG_BUILD_PATH%" /s/q
)

echo.
echo === Building ICU for Clang x64 ===
mkdir "%CLANG_BUILD_PATH%"
cd /d "%CLANG_BUILD_PATH%"
cmake -G "Visual Studio 17 2022" -A x64 -T ClangCL "%PATH_TO_CMAKE_FILE%"
if %ERRORLEVEL% neq 0 (
	echo ERROR: CMake generation failed for Clang x64.
	exit /b 1
)

echo Building Clang x64 Debug...
"%MSBUILD_EXE%" icu.sln /p:Configuration=Debug /p:Platform=x64 /m || exit /b
echo Building Clang x64 Release...
"%MSBUILD_EXE%" icu.sln /p:Configuration=Release /p:Platform=x64 /m || exit /b

if not exist "%CLANG_OUTPUT_PATH%\Debug" (
	mkdir "%CLANG_OUTPUT_PATH%\Debug"
)
if not exist "%CLANG_OUTPUT_PATH%\Release" (
	mkdir "%CLANG_OUTPUT_PATH%\Release"
)
copy /B/Y "%CLANG_BUILD_PATH%\Debug\icu.lib" "%CLANG_OUTPUT_PATH%\Debug\icu.lib"
copy /B/Y "%CLANG_BUILD_PATH%\Release\icu.lib" "%CLANG_OUTPUT_PATH%\Release\icu.lib"

cd /d "%~dp0"
rmdir "%CLANG_BUILD_PATH%" /s/q

echo.
echo Done.
endlocal
