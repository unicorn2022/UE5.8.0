@rem Copyright Epic Games, Inc. All Rights Reserved.
@echo off

set LIBCURL_VERSION=8.17.0
set OPENSSL_VERSION=1.1.1t
set ZLIB_VERSION=1.3
set NGHTTP2_VERSION=1.64.0

set ENGINE_ROOT=%~dp0..\..\..\..\..\..
set THIRDPARTY_ROOT=%ENGINE_ROOT%\Source\ThirdParty

rem Common CMake arguments (without ENABLE_DEBUG)
set CMAKE_COMMON_ARGUMENTS=-DCMAKE_C_FLAGS='-DNGHTTP2_STATICLIB=1 /Zi' -DUSE_NGHTTP2=ON -DCURL_USE_OPENSSL=ON -DBUILD_CURL_EXE=OFF -DCURL_DISABLE_LDAP=ON -DCURL_DISABLE_LDAPS=ON -DBUILD_SHARED_LIBS=OFF -DSHARE_LIB_OBJECT=OFF -DCMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH=OFF -DCURL_USE_LIBPSL=OFF

rem Win64 x64 builds
set CMAKE_OPENSSL_ARGUMENTS=-DOPENSSL_ROOT_DIR=%THIRDPARTY_ROOT%\OpenSSL\%OPENSSL_VERSION%\lib\Win64\VS2015\Release -DOPENSSL_INCLUDE_DIR=%THIRDPARTY_ROOT%\OpenSSL\%OPENSSL_VERSION%\include\Win64\VS2015 -DOPENSSL_USE_STATIC_LIBS=ON
set CMAKE_ZLIB_ARGUMENTS=-DZLIB_ROOT=%THIRDPARTY_ROOT%\zlib\%ZLIB_VERSION%\lib\Win64\Release -DZLIB_INCLUDE_DIR=%THIRDPARTY_ROOT%\zlib\%ZLIB_VERSION%\include
set CMAKE_NGHTTP2_ARGUMENTS=-DNGHTTP2_LIBRARY=%THIRDPARTY_ROOT%\nghttp2\%NGHTTP2_VERSION%\lib\Win64\Release\nghttp2.lib -DNGHTTP2_INCLUDE_DIR=%THIRDPARTY_ROOT%\nghttp2\%NGHTTP2_VERSION%\include

rem Build Debug with ENABLE_DEBUG=ON (enables DEBUGBUILD macro and curl_global_assert_handler)
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetLib=libcurl -TargetLibVersion=%LIBCURL_VERSION% -TargetConfigs=Debug -LibOutputPath=lib -CMakeGenerator=VS2022 -CMakeAdditionalArguments="%CMAKE_COMMON_ARGUMENTS% -DENABLE_DEBUG=ON %CMAKE_OPENSSL_ARGUMENTS% %CMAKE_ZLIB_ARGUMENTS% %CMAKE_NGHTTP2_ARGUMENTS%" -SkipCreateChangelist || exit /b

rem Build Release without ENABLE_DEBUG
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetLib=libcurl -TargetLibVersion=%LIBCURL_VERSION% -TargetConfigs=Release -LibOutputPath=lib -CMakeGenerator=VS2022 -CMakeAdditionalArguments="%CMAKE_COMMON_ARGUMENTS% %CMAKE_OPENSSL_ARGUMENTS% %CMAKE_ZLIB_ARGUMENTS% %CMAKE_NGHTTP2_ARGUMENTS%" -SkipCreateChangelist || exit /b


rem Now do it all again for Arm64!

set CMAKE_OPENSSL_ARGUMENTS=-DOPENSSL_ROOT_DIR=%THIRDPARTY_ROOT%\OpenSSL\%OPENSSL_VERSION%\lib\WinArm64\Release -DOPENSSL_INCLUDE_DIR=%THIRDPARTY_ROOT%\OpenSSL\%OPENSSL_VERSION%\include\WinArm64 -DOPENSSL_USE_STATIC_LIBS=ON
set CMAKE_ZLIB_ARGUMENTS=-DZLIB_ROOT=%THIRDPARTY_ROOT%\zlib\%ZLIB_VERSION%\lib\Win64\arm64\Release -DZLIB_INCLUDE_DIR=%THIRDPARTY_ROOT%\zlib\%ZLIB_VERSION%\include
set CMAKE_NGHTTP2_ARGUMENTS=-DNGHTTP2_LIBRARY=%THIRDPARTY_ROOT%\nghttp2\%NGHTTP2_VERSION%\lib\Win64\arm64\Release\nghttp2.lib -DNGHTTP2_INCLUDE_DIR=%THIRDPARTY_ROOT%\nghttp2\%NGHTTP2_VERSION%\include

rem Build Debug with ENABLE_DEBUG=ON for Arm64
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetArchitecture=Arm64 -TargetLib=libcurl -TargetLibVersion=%LIBCURL_VERSION% -TargetConfigs=Debug -LibOutputPath=lib -CMakeGenerator=VS2022 -CMakeAdditionalArguments="%CMAKE_COMMON_ARGUMENTS% -DENABLE_DEBUG=ON %CMAKE_OPENSSL_ARGUMENTS% %CMAKE_ZLIB_ARGUMENTS% %CMAKE_NGHTTP2_ARGUMENTS%" -SkipCreateChangelist || exit /b

rem Build Release without ENABLE_DEBUG for Arm64
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Win64 -TargetArchitecture=Arm64 -TargetLib=libcurl -TargetLibVersion=%LIBCURL_VERSION% -TargetConfigs=Release -LibOutputPath=lib -CMakeGenerator=VS2022 -CMakeAdditionalArguments="%CMAKE_COMMON_ARGUMENTS% %CMAKE_OPENSSL_ARGUMENTS% %CMAKE_ZLIB_ARGUMENTS% %CMAKE_NGHTTP2_ARGUMENTS%" -SkipCreateChangelist || exit /b
