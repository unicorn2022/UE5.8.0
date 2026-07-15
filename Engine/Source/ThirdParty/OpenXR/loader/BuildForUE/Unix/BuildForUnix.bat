@rem Copyright Epic Games, Inc. All Rights Reserved.
@echo off

rem unzip the openxr sdk release into:
rem Engine\Source\ThirdParty\OpenXR\loader\BuildForUE\OpenXR-SDK-release-1.1.57\OpenXR-SDK-release-1.1.57
rem update the version string below if necessary.
rem The sdk may have issues with xlib dependencies that it tries to pull in even though we are only building the loader 
rem claude can fix those easily.
rem patch presentation.cmake to only error when tests are actually being built
rem     if(PRESENTATION_BACKEND MATCHES "xlib")
rem         if(NOT BUILD_WITH_XLIB_HEADERS)
rem  -          message(
rem  -              FATAL_ERROR
rem  -                  "xlib backend selected, but BUILD_WITH_XLIB_HEADERS either disabled or unavailable
rem  - due to missing dependencies."
rem  -          )
rem  +          if(BUILD_TESTS OR BUILD_CONFORMANCE_TESTS OR BUILD_SDK_TESTS)
rem  +              message(
rem  +                  FATAL_ERROR
rem  +                      "xlib backend selected, but BUILD_WITH_XLIB_HEADERS either disabled or
rem  + unavailable due to missing dependencies."
rem  +              )
rem  +          else()
rem  +              message(STATUS "xlib backend unavailable, but skipping since tests are disabled")
rem  +          endif()
rem         endif()
rem         if(BUILD_TESTS AND (NOT X11_Xxf86vm_LIB OR NOT X11_Xrandr_LIB))
rem             message(FATAL_ERROR "OpenXR tests using xlib backend requires Xxf86vm and Xrandr")
rem patch CMakeLists.txt like:
rem -  elseif(PRESENTATION_BACKEND MATCHES "xlib")
rem +  elseif(PRESENTATION_BACKEND MATCHES "xlib" AND BUILD_WITH_XLIB_HEADERS)
rem        add_definitions(-DXR_USE_PLATFORM_XLIB)
rem -  elseif(PRESENTATION_BACKEND MATCHES "xcb")
rem +  elseif(PRESENTATION_BACKEND MATCHES "xcb" AND BUILD_WITH_XCB_HEADERS)
rem        add_definitions(-DXR_USE_PLATFORM_XCB)

set VERSION=1.1.57

set ENGINE_ROOT=%~dp0..\..\..\..\..\..
set THIRDPARTY_ROOT=%ENGINE_ROOT%\Source\ThirdParty
set LOADER_ROOT=%THIRDPARTY_ROOT%\OpenXR\loader
set TARGET_LIB_SOURCE_PATH=%LOADER_ROOT%\BuildForUE\OpenXR-SDK-release-%VERSION%\OpenXR-SDK-release-%VERSION%

rem Common CMake arguments, we only want to build the loader
set CMAKE_COMMON_ARGUMENTS=-DBUILD_TESTS=OFF -DBUILD_CONFORMANCE_TESTS=OFF -DBUILD_WITH_XLIB_HEADERS=OFF -DBUILD_WITH_XCB_HEADERS=OFF -DBUILD_WITH_WAYLAND_HEADERS=OFF -DDYNAMIC_LOADER=ON

rem I did not test the x86-64 build
rem x86_64-unknown-linux-gnu builds
rem call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Unix -TargetArchitecture=x86_64-unknown-linux-gnu -TargetLib=openxr_loader -TargetLibVersion=%VERSION% -TargetConfigs=Debug -TargetLibSourcePath=%TARGET_LIB_SOURCE_PATH% -LibOutputPath=lib -CMakeGenerator=Makefile -MakeTarget=openxr_loader -CMakeAdditionalArguments="%CMAKE_COMMON_ARGUMENTS%" -SkipCreateChangelist || exit /b
rem call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Unix -TargetArchitecture=x86_64-unknown-linux-gnu -TargetLib=openxr_loader -TargetLibVersion=%VERSION% -TargetConfigs=Release -TargetLibSourcePath=%TARGET_LIB_SOURCE_PATH% -LibOutputPath=lib -CMakeGenerator=Makefile -MakeTarget=openxr_loader -CMakeAdditionalArguments="%CMAKE_COMMON_ARGUMENTS%" -SkipCreateChangelist || exit /b

rem aarch64-unknown-linux-gnueabi builds
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Unix -TargetArchitecture=aarch64-unknown-linux-gnueabi -TargetLib=openxr_loader -TargetLibVersion=%VERSION% -TargetConfigs=Debug -TargetLibSourcePath=%TARGET_LIB_SOURCE_PATH% -LibOutputPath=lib -CMakeGenerator=Makefile -MakeTarget=openxr_loader -CMakeAdditionalArguments="%CMAKE_COMMON_ARGUMENTS%" -SkipCreateChangelist || exit /b
call "%ENGINE_ROOT%\Build\BatchFiles\RunUAT.bat" BuildCMakeLib -TargetPlatform=Unix -TargetArchitecture=aarch64-unknown-linux-gnueabi -TargetLib=openxr_loader -TargetLibVersion=%VERSION% -TargetConfigs=Release -TargetLibSourcePath=%TARGET_LIB_SOURCE_PATH% -LibOutputPath=lib -CMakeGenerator=Makefile -MakeTarget=openxr_loader -CMakeAdditionalArguments="%CMAKE_COMMON_ARGUMENTS%" -SkipCreateChangelist || exit /b
