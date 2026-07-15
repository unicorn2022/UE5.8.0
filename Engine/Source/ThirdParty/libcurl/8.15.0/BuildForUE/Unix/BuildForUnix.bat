@rem Copyright Epic Games, Inc. All Rights Reserved.
@echo off

set LIBCURL_VERSION=8.15.0
set OPENSSL_VERSION=1.1.1t
set ZLIB_VERSION=1.3
set NGHTTP2_VERSION=1.64.0

set ENGINE_ROOT=%~dp0..\..\..\..\..\..
set THIRDPARTY_ROOT=%ENGINE_ROOT%\Source\ThirdParty

rem Common CMake arguments (without ENABLE_DEBUG)
set CMAKE_COMMON_ARGUMENTS=-DCMAKE_C_FLAGS="-DNGHTTP2_STATICLIB=1" -DUSE_NGHTTP2=ON -DCURL_USE_OPENSSL=ON -DBUILD_CURL_EXE=OFF -DCURL_DISABLE_LDAP=ON -DCURL_DISABLE_LDAPS=ON -DBUILD_SHARED_LIBS=OFF -DCMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH=OFF -DCURL_USE_LIBPSL=OFF

rem x86_64-unknown-linux-gnu builds
set CMAKE_OPENSSL_ARGUMENTS=-DOPENSSL_CRYPTO_LIBRARY=%THIRDPARTY_ROOT%\OpenSSL\%OPENSSL_VERSION%\lib\Unix\x86_64-unknown-linux-gnu\libcrypto.a -DOPENSSL_SSL_LIBRARY=%THIRDPARTY_ROOT%\OpenSSL\%OPENSSL_VERSION%\lib\Unix\x86_64-unknown-linux-gnu\libssl.a -DOPENSSL_INCLUDE_DIR=%THIRDPARTY_ROOT%\OpenSSL\%OPENSSL_VERSION%\include\Unix -DOPENSSL_USE_STATIC_LIBS=ON
set CMAKE_ZLIB_ARGUMENTS=-DZLIB_LIBRARY=%THIRDPARTY_ROOT%\zlib\%ZLIB_VERSION%\lib\Unix\x86_64-unknown-linux-gnu\Release\libz.a -DZLIB_INCLUDE_DIR=%THIRDPARTY_ROOT%\zlib\%ZLIB_VERSION%\include
set CMAKE_NGHTTP2_ARGUMENTS=-DNGHTTP2_LIBRARY=%THIRDPARTY_ROOT%\nghttp2\%NGHTTP2_VERSION%\lib\Unix\x86_64-unknown-linux-gnu\Release\libnghttp2.a -DNGHTTP2_INCLUDE_DIR=%THIRDPARTY_ROOT%\nghttp2\%NGHTTP2_VERSION%\include

rem Build Debug with ENABLE_DEBUG=ON for x86_64 (enables DEBUGBUILD macro and curl_global_assert_handler)
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Unix -TargetArchitecture=x86_64-unknown-linux-gnu -TargetLib=libcurl -TargetLibVersion=%LIBCURL_VERSION% -TargetConfigs=Debug -LibOutputPath=lib -CMakeGenerator=Makefile -CMakeAdditionalArguments="%CMAKE_COMMON_ARGUMENTS% -DENABLE_DEBUG=ON %CMAKE_OPENSSL_ARGUMENTS% %CMAKE_ZLIB_ARGUMENTS% %CMAKE_NGHTTP2_ARGUMENTS%" -SkipCreateChangelist || exit /b

rem Build Release without ENABLE_DEBUG for x86_64
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Unix -TargetArchitecture=x86_64-unknown-linux-gnu -TargetLib=libcurl -TargetLibVersion=%LIBCURL_VERSION% -TargetConfigs=Release -LibOutputPath=lib -CMakeGenerator=Makefile -CMakeAdditionalArguments="%CMAKE_COMMON_ARGUMENTS% %CMAKE_OPENSSL_ARGUMENTS% %CMAKE_ZLIB_ARGUMENTS% %CMAKE_NGHTTP2_ARGUMENTS%" -SkipCreateChangelist || exit /b

rem aarch64-unknown-linux-gnueabi builds
set CMAKE_OPENSSL_ARGUMENTS=-DOPENSSL_CRYPTO_LIBRARY=%THIRDPARTY_ROOT%\OpenSSL\%OPENSSL_VERSION%\lib\Unix\aarch64-unknown-linux-gnueabi\libcrypto.a -DOPENSSL_SSL_LIBRARY=%THIRDPARTY_ROOT%\OpenSSL\%OPENSSL_VERSION%\lib\Unix\aarch64-unknown-linux-gnueabi\libssl.a -DOPENSSL_INCLUDE_DIR=%THIRDPARTY_ROOT%\OpenSSL\%OPENSSL_VERSION%\include\Unix -DOPENSSL_USE_STATIC_LIBS=ON
set CMAKE_ZLIB_ARGUMENTS=-DZLIB_LIBRARY=%THIRDPARTY_ROOT%\zlib\%ZLIB_VERSION%\lib\Unix\aarch64-unknown-linux-gnueabi\Release\libz.a -DZLIB_INCLUDE_DIR=%THIRDPARTY_ROOT%\zlib\%ZLIB_VERSION%\include
set CMAKE_NGHTTP2_ARGUMENTS=-DNGHTTP2_LIBRARY=%THIRDPARTY_ROOT%\nghttp2\%NGHTTP2_VERSION%\lib\Unix\aarch64-unknown-linux-gnueabi\Release\libnghttp2.a -DNGHTTP2_INCLUDE_DIR=%THIRDPARTY_ROOT%\nghttp2\%NGHTTP2_VERSION%\include

rem Build Debug with ENABLE_DEBUG=ON for aarch64
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Unix -TargetArchitecture=aarch64-unknown-linux-gnueabi -TargetLib=libcurl -TargetLibVersion=%LIBCURL_VERSION% -TargetConfigs=Debug -LibOutputPath=lib -CMakeGenerator=Makefile -CMakeAdditionalArguments="%CMAKE_COMMON_ARGUMENTS% -DENABLE_DEBUG=ON %CMAKE_OPENSSL_ARGUMENTS% %CMAKE_ZLIB_ARGUMENTS% %CMAKE_NGHTTP2_ARGUMENTS%" -SkipCreateChangelist || exit /b

rem Build Release without ENABLE_DEBUG for aarch64
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Unix -TargetArchitecture=aarch64-unknown-linux-gnueabi -TargetLib=libcurl -TargetLibVersion=%LIBCURL_VERSION% -TargetConfigs=Release -LibOutputPath=lib -CMakeGenerator=Makefile -CMakeAdditionalArguments="%CMAKE_COMMON_ARGUMENTS% %CMAKE_OPENSSL_ARGUMENTS% %CMAKE_ZLIB_ARGUMENTS% %CMAKE_NGHTTP2_ARGUMENTS%" -SkipCreateChangelist || exit /b
