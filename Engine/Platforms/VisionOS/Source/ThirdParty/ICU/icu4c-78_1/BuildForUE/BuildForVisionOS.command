#!/bin/zsh -eux
# Copyright Epic Games, Inc. All Rights Reserved.

# We set this to ensure the version of Vision OS we're building for, otherwise we could build the library in a higher version than what UE supports
export XROS_DEPLOYMENT_TARGET=1.0
ENGINE_ROOT=${0:a:h:h:h:h:h:h:h:h}
THIRD_PARTY_DIRECTORY=${ENGINE_ROOT}/Source/ThirdParty
VISIONOS_THIRD_PARTY=${ENGINE_ROOT}/Platforms/VisionOS/Source/ThirdParty

TOOLCHAIN_FILE=${VISIONOS_THIRD_PARTY}/CMake/PlatformScripts/VisionOS.cmake
ICU_VISIONOS_VERSION=icu4c-78_1
PATH_TO_ICU=${THIRD_PARTY_DIRECTORY}/ICU/${ICU_VISIONOS_VERSION}

CMAKE_ADDITIONAL_ARGUMENTS=-DCMAKE_OSX_DEPLOYMENT_TARGET=1.0
# Uncomment this to output debug messages while building the library
#CMAKE_ADDITIONAL_ARGUMENTS+=" -DCMAKE_FIND_DEBUG_MODE=ON"
CMAKE_ADDITIONAL_ARGUMENTS+=" -DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}"

MAKE_TARGET=icu

for ARCH in arm64 x86_64 xrsimulator;
do
  "${ENGINE_ROOT}/Build/BatchFiles/RunUAT.command" BuildCMakeLib -TargetPlatform=VisionOS -TargetArchitecture=${ARCH} -TargetLib=ICU -TargetLibVersion=${ICU_VISIONOS_VERSION} -TargetConfigs=Debug+Release -LibOutputPath=lib -CMakeGenerator=Makefile -CMakeAdditionalArguments="${CMAKE_ADDITIONAL_ARGUMENTS}" -MakeTarget=${MAKE_TARGET} -SkipCreateChangelist
done

pushd "${VISIONOS_THIRD_PARTY}/ICU/${ICU_VISIONOS_VERSION}/lib"
mkdir -p Debug Release Simulator Simulator/Debug Simulator/Release
mv arm64/Debug/libicu.a Debug/libicu.a
mv arm64/Release/libicu.a Release/libicu.a
lipo -create xrsimulator/Debug/libicu.a x86_64/Debug/libicu.a -output Simulator/Debug/libicu.a
lipo -create xrsimulator/Release/libicu.a x86_64/Release/libicu.a -output Simulator/Release/libicu.a
rm -rf arm64 x86_64 xrsimulator
popd
