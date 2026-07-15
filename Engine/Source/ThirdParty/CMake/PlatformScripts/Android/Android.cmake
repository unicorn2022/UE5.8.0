# Copyright Epic Games, Inc. All Rights Reserved.
#
# Android toolchain: layers UE defaults onto the NDK-shipped toolchain.
# NDK provides compiler/sysroot/ABI/hardening; UE adds c++_static STL,
# C++20 default, and a few extra flags. Per-arch wrappers (Android-ARM64.cmake,
# Android-x64.cmake) set CMAKE_ANDROID_ARCH_ABI before including this file.

# Reentrancy guard - CMake re-loads the toolchain during try_compile.
if(UE_ANDROID_TOOLCHAIN_INCLUDED)
	return()
endif()
set(UE_ANDROID_TOOLCHAIN_INCLUDED TRUE)

# --- Validate NDK install -------------------------------------------------

if(NOT DEFINED ENV{ANDROID_NDK_ROOT})
	message(FATAL_ERROR "ANDROID_NDK_ROOT environment variable is not set!")
endif()
file(TO_CMAKE_PATH $ENV{ANDROID_NDK_ROOT} CMAKE_ANDROID_NDK)
if(NOT EXISTS ${CMAKE_ANDROID_NDK})
	message(FATAL_ERROR "ANDROID_NDK_ROOT must point to the NDK directory!")
endif()
if(NOT EXISTS "${CMAKE_ANDROID_NDK}/meta/platforms.json")
	message(FATAL_ERROR "NDK at ${CMAKE_ANDROID_NDK} does not contain meta/platforms.json!")
endif()

# --- API level (default 26), validated against the installed NDK ---------

if(NOT CMAKE_SYSTEM_VERSION)
	set(CMAKE_SYSTEM_VERSION 26)
endif()
file(READ "${CMAKE_ANDROID_NDK}/meta/platforms.json" NDK_PLATFORMS)
string(JSON NDK_API_MIN GET "${NDK_PLATFORMS}" "min")
string(JSON NDK_API_MAX GET "${NDK_PLATFORMS}" "max")
if((CMAKE_SYSTEM_VERSION LESS NDK_API_MIN) OR (NDK_API_MAX LESS CMAKE_SYSTEM_VERSION))
	message(FATAL_ERROR "NDK at ${CMAKE_ANDROID_NDK} supports API ${NDK_API_MIN}-${NDK_API_MAX}, requested ${CMAKE_SYSTEM_VERSION}!")
endif()

# --- NDK input defaults (callers can override via -D) --------------------

set(ANDROID_NDK "${CMAKE_ANDROID_NDK}")
if(NOT DEFINED ANDROID_NATIVE_API_LEVEL)
	set(ANDROID_NATIVE_API_LEVEL "android-${CMAKE_SYSTEM_VERSION}")
endif()
if(NOT DEFINED ANDROID_PLATFORM)
	set(ANDROID_PLATFORM "android-${CMAKE_SYSTEM_VERSION}")
endif()
# ABI comes from the per-arch wrapper's CMAKE_ANDROID_ARCH_ABI; default arm64-v8a.
if(NOT DEFINED ANDROID_ABI)
	if(DEFINED CMAKE_ANDROID_ARCH_ABI)
		set(ANDROID_ABI ${CMAKE_ANDROID_ARCH_ABI})
	else()
		set(ANDROID_ABI arm64-v8a)
	endif()
endif()
if(NOT DEFINED ANDROID_STL)
	set(ANDROID_STL c++_static)
endif()
if(NOT DEFINED CMAKE_POSITION_INDEPENDENT_CODE)
	set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif()
# Modern NDK code path cooperates with target_compile_features; legacy doesn't.
if(NOT DEFINED ANDROID_USE_LEGACY_TOOLCHAIN_FILE)
	set(ANDROID_USE_LEGACY_TOOLCHAIN_FILE FALSE)
endif()

# --- Layer 1: NDK toolchain (compiler, sysroot, hardening, linker flags) -

include("${CMAKE_ANDROID_NDK}/build/cmake/android.toolchain.cmake")

# --- Layer 2: UE defaults on top -----------------------------------------

# All libraries built with this toolchain compile against C++20.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Lock STL to c++_static; final UE binary manages C++ runtime linkage.
set(CMAKE_ANDROID_STL_TYPE "c++_static" CACHE STRING "Android STL type" FORCE)

# Append UE flags to _INIT so they ride alongside NDK's preset; appending to
# CMAKE_*_FLAGS directly does NOT reach the per-target compile line.
string(APPEND CMAKE_C_FLAGS_INIT   " -fno-short-enums -fno-strict-aliasing -g2 -gdwarf-4")
string(APPEND CMAKE_CXX_FLAGS_INIT " -fno-short-enums -fno-strict-aliasing -g2 -gdwarf-4")

# --- Diagnostics ---------------------------------------------------------

if(NOT DEFINED CMAKE_VERBOSE_MAKEFILE)
	set(CMAKE_VERBOSE_MAKEFILE ON)
endif()
message(STATUS "[UE Android] NDK:                  ${CMAKE_ANDROID_NDK}")
message(STATUS "[UE Android] API level:            ${ANDROID_NATIVE_API_LEVEL}")
message(STATUS "[UE Android] ABI:                  ${ANDROID_ABI}")
message(STATUS "[UE Android] STL:                  ${ANDROID_STL}")
message(STATUS "[UE Android] CXX standard:         ${CMAKE_CXX_STANDARD}")
message(STATUS "[UE Android] CMAKE_C_FLAGS_INIT:   ${CMAKE_C_FLAGS_INIT}")
message(STATUS "[UE Android] CMAKE_CXX_FLAGS_INIT: ${CMAKE_CXX_FLAGS_INIT}")
