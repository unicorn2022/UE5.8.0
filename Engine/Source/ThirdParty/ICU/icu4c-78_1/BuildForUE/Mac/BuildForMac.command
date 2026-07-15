#!/bin/zsh -eux
# Copyright Epic Games, Inc. All Rights Reserved.

ENGINE_ROOT=${0:a:h:h:h:h:h:h:h}
THIRD_PARTY_DIRECTORY=${ENGINE_ROOT}/Source/ThirdParty

TOOLCHAIN_FILE=${ENGINE_ROOT}/Source/ThirdParty/CMake/PlatformScripts/Mac/Mac.cmake
ICU_MAC_VERSION=icu4c-78_1
PATH_TO_ICU=${THIRD_PARTY_DIRECTORY}/ICU/${ICU_MAC_VERSION}

CMAKE_ADDITIONAL_ARGUMENTS=-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
# Uncomment this to output debug messages while building the library
#CMAKE_ADDITIONAL_ARGUMENTS+=" -DCMAKE_FIND_DEBUG_MODE=ON"
CMAKE_ADDITIONAL_ARGUMENTS+=" -DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}"

MAKE_TARGET=icu

"${ENGINE_ROOT}/Source/ThirdParty/CMake/PlatformScripts/Mac/BuildLibForMac.command" ICU ${ICU_MAC_VERSION} --cmake-args="${CMAKE_ADDITIONAL_ARGUMENTS}" --make-target=${MAKE_TARGET}
