@echo off
setlocal EnableDelayedExpansion

if not exist "ShaderConductor" goto IncorrectWorkingDir


echo 
echo ************************************
echo *** Make sure to run the regular Win64 script first, WinARM64 requires some executables to run on the host platform.
echo ************************************


set CONFIG=RelWithDebInfo
REM Run build script with "-debug" argument to get debuggable shader conductor
if "%1"=="-debug" set CONFIG=Debug

REM The version of Visual Studio tools to use for compilation, this must match what is used by CIS build machines.  
REM (just search your email for MSVC, version bumps are always broadcasted)
REM NOTE: Different versions can be installed through the Visual Studio Installer in the "individual component" view by filtering for MSVC.
REM NOTE: Be sure to also install the corresponding ATL package for x64 AMD ARM64.
set VS_VERSION_STRING=";VisualStudioVersion=17.8"

set ENGINE_THIRD_PARTY_BIN=..\..\Binaries\ThirdParty\ShaderConductor\WinArm64
set ENGINE_THIRD_PARTY_SOURCE=..\..\Source\ThirdParty\ShaderConductor

set VSPROJ_SHADERCONDUCTOR_LIB=ALL_BUILD.vcxproj
set VSPROJ_DXCOMPILER_APP=External\DirectXShaderCompiler\tools\clang\tools\dxc\dxc.vcxproj

if not exist "..\%ENGINE_THIRD_PARTY_BIN%" goto IncorrectWorkingDir

if exist ShaderConductor\lib\WinArm64 goto Continue
echo 
echo ************************************
echo *** Creating ShaderConductor\lib\WinArm64...
mkdir ShaderConductor\lib
mkdir ShaderConductor\lib\WinArm64

:Continue
echo 
echo ************************************
echo *** Checking out files...
pushd ..\%ENGINE_THIRD_PARTY_BIN%
	p4 edit ./...
popd

pushd ShaderConductor\lib\WinArm64
	p4 edit ./...
popd

mkdir ..\..\..\Intermediate\ShaderConductorWinARM64
pushd ..\..\..\Intermediate\ShaderConductorWinARM64


	set CLANG_TABLEGEN=%CD%\..\ShaderConductor\External\DirectXShaderCompiler\RelWithDebInfo\bin\clang-tblgen.exe
	set LLVM_TABLEGEN=%CD%\..\ShaderConductor\External\DirectXShaderCompiler\RelWithDebInfo\bin\llvm-tblgen.exe
	
	echo 
	echo ************************************
	echo *** CMake
	echo *** Using clang-tblgen.exe from Win build: %CLANG_TABLEGEN%
	echo *** Using llvm-tblgen.exe from Win build: %LLVM_TABLEGEN%
	cmake -G "Visual Studio 17 2022" -A arm64 -DCLANG_TABLEGEN="%CLANG_TABLEGEN%" -DLLVM_TABLEGEN="%LLVM_TABLEGEN%" %ENGINE_THIRD_PARTY_SOURCE%\ShaderConductor

	echo 
	echo ************************************
	echo *** MSBuild
	
	MSbuild.exe "%VSPROJ_SHADERCONDUCTOR_LIB%" -nologo -v:m -maxCpuCount -p:Platform=arm64;Configuration="%CONFIG%%VS_VERSION_STRING%"
	MSbuild.exe "%VSPROJ_DXCOMPILER_APP%" -nologo -v:m -maxCpuCount -p:Platform=arm64;Configuration="%CONFIG%%VS_VERSION_STRING%"

	
	
	echo 
	echo ************************************
	echo *** Copying to final destination
 	xcopy External\DirectXShaderCompiler\%CONFIG%\bin\dxc.pdb			%ENGINE_THIRD_PARTY_BIN%\dxc.pdb  /F /Y
 	xcopy External\DirectXShaderCompiler\%CONFIG%\bin\dxc.exe			%ENGINE_THIRD_PARTY_BIN%\dxc.exe  /F /Y
 	xcopy External\DirectXShaderCompiler\%CONFIG%\bin\dxcompiler.pdb	%ENGINE_THIRD_PARTY_BIN%\dxcompiler.pdb  /F /Y
 	xcopy Bin\%CONFIG%\dxcompiler.dll									%ENGINE_THIRD_PARTY_BIN%\dxcompiler.dll  /F /Y
 	xcopy Bin\%CONFIG%\ShaderConductor.dll								%ENGINE_THIRD_PARTY_BIN%\ShaderConductor.dll  /F /Y
 	xcopy Bin\%CONFIG%\ShaderConductor.pdb								%ENGINE_THIRD_PARTY_BIN%\ShaderConductor.pdb  /F /Y
	xcopy Lib\%CONFIG%\ShaderConductor.lib								%ENGINE_THIRD_PARTY_SOURCE%\ShaderConductor\lib\WinArm64  /F /Y
	xcopy Bin\%CONFIG%\ShaderConductorCmd.pdb							%ENGINE_THIRD_PARTY_BIN%\ShaderConductorCmd.pdb  /F /Y
	xcopy Bin\%CONFIG%\ShaderConductorCmd.exe							%ENGINE_THIRD_PARTY_BIN%\ShaderConductorCmd.exe  /F /Y
popd

goto :EOF

:IncorrectWorkingDir
echo ERROR: This script must be run from the Engine\Source\ThirdParty\ShaderConductor directory.
