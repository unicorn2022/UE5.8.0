@ECHO OFF

REM Copyright Epic Games, Inc. All Rights Reserved.

setlocal EnableDelayedExpansion

set ROOT_DIR=%~dp0..\..
set ICU_VERSION=78
set ICU_DATA_DIR=icudt%ICU_VERSION%l
set SRC_DIR=%ROOT_DIR%\source
set TOOL_DIR=%ROOT_DIR%\source\tools
set TOOL_CFG=x64\Release
set OUT_DIR=%ROOT_DIR%\icu-data
set TMP_DIR=%ROOT_DIR%\icu-data-tmp
set FILTER_DIR=%~dp0Filters
set PYTHONPATH=%SRC_DIR%\python

REM Verify prerequisites
where py >nul 2>&1
if %ERRORLEVEL% neq 0 (
	echo ERROR: Python 3 not found on PATH.
	exit /b 1
)

set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist %VSWHERE% (
	echo ERROR: vswhere.exe not found. Is Visual Studio installed?
	exit /b 1
)

for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -requires Microsoft.Component.MSBuild -property installationPath`) do set VS_DIR=%%i
set MSBUILD_EXE="%VS_DIR%\MSBuild\Current\Bin\MSBuild.exe"
if not exist %MSBUILD_EXE% (
	echo ERROR: MSBuild.exe not found at %MSBUILD_EXE%
	exit /b 1
)
echo Found Visual Studio at: %VS_DIR%

REM Build ICU makedata tool
echo Building ICU makedata tool...
%MSBUILD_EXE% "%SRC_DIR%\allinone\allinone.sln" /t:makedata /p:Configuration=Release /p:Platform=x64 /m /nologo /v:normal
if %ERRORLEVEL% neq 0 (
	echo ERROR: Failed to build ICU makedata tools. See MSBuild output above.
	exit /b 1
)

REM Add ICU DLL directory to PATH so tools can find their dependencies
set PATH=%ROOT_DIR%\bin64;%PATH%

REM Build each set of filtered ICU data
echo Building filtered ICU data sets...
set PRESETS=English EFIGS EFIGSCJK CJK All

cd "%SRC_DIR%\data"
for %%P in (%PRESETS%) do (
	echo    Building: %%P
	py -B -m icutools.databuilder ^
		--mode windows-exec ^
		--src_dir "%SRC_DIR%\data" ^
		--tool_dir "%TOOL_DIR%" ^
		--tool_cfg "%TOOL_CFG%" ^
		--out_dir "%OUT_DIR%\%%P\%ICU_DATA_DIR%" ^
		--tmp_dir "%TMP_DIR%\%%P" ^
		--filter_file "%FILTER_DIR%\%%P.json"
	if !ERRORLEVEL! neq 0 (
		echo ERROR: Failed to build data set %%P
		exit /b 1
	)
)
cd %ROOT_DIR%

echo.
echo Build complete. Output in: %OUT_DIR%
echo.
echo Copy each preset to Engine/Content/Internationalization/
echo Copy the All preset also to Engine/Content/Internationalization/%ICU_DATA_DIR%/ (for the editor)
