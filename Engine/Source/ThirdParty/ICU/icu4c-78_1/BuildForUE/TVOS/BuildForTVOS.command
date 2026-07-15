#!/bin/zsh -eux
# Copyright Epic Games, Inc. All Rights Reserved.

ENGINE_ROOT=${0:a:h:h:h:h:h:h:h}
THIRD_PARTY_DIRECTORY=${ENGINE_ROOT}/Source/ThirdParty

TOOLCHAIN_FILE=${ENGINE_ROOT}/Source/ThirdParty/CMake/PlatformScripts/TVOS/TVOS.cmake
ICU_TVOS_VERSION=icu4c-78_1
PATH_TO_ICU=${THIRD_PARTY_DIRECTORY}/ICU/${ICU_TVOS_VERSION}

CMAKE_ADDITIONAL_ARGUMENTS=-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
# Uncomment this to output debug messages while building the library
#CMAKE_ADDITIONAL_ARGUMENTS+=" -DCMAKE_FIND_DEBUG_MODE=ON"
CMAKE_ADDITIONAL_ARGUMENTS+=" -DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}"

MAKE_TARGET=icu

for ARCH in arm64 x86_64 tvossimulator;
do
  "${ENGINE_ROOT}/Build/BatchFiles/RunUAT.command" BuildCMakeLib -TargetPlatform=TVOS -TargetArchitecture=${ARCH} -TargetLib=ICU -TargetLibVersion=${ICU_TVOS_VERSION} -TargetConfigs=Debug+Release -LibOutputPath=lib -CMakeGenerator=Makefile -CMakeAdditionalArguments="${CMAKE_ADDITIONAL_ARGUMENTS}" -MakeTarget=${MAKE_TARGET} -SkipCreateChangelist
done

pushd "${ENGINE_ROOT}/Source/ThirdParty/ICU/${ICU_TVOS_VERSION}/lib/TVOS"
mkdir -p Debug Release Simulator Simulator/Debug Simulator/Release
mv arm64/Debug/libicu.a Debug/libicu.a
mv arm64/Release/libicu.a Release/libicu.a
lipo -create tvossimulator/Debug/libicu.a x86_64/Debug/libicu.a -output Simulator/Debug/libicu.a
lipo -create tvossimulator/Release/libicu.a x86_64/Release/libicu.a -output Simulator/Release/libicu.a
rm -rf arm64 x86_64 tvossimulator
popd
