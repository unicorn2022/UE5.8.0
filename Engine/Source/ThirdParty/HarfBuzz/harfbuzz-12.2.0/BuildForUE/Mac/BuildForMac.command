#!/bin/zsh -eux
# Copyright Epic Games, Inc. All Rights Reserved.
#
# Builds HarfBuzz 12.2.0 static libraries for macOS (arm64 + x86_64) against
# ICU 78 and FreeType2-2.14.1. Outputs universal static libraries to:
#   harfbuzz-12.2.0/lib/Mac/{Debug,Release}/libharfbuzz.a

ENGINE_ROOT=${0:a:h:h:h:h:h:h:h}
THIRD_PARTY_DIRECTORY=${ENGINE_ROOT}/Source/ThirdParty

TOOLCHAIN_FILE=${ENGINE_ROOT}/Source/ThirdParty/CMake/PlatformScripts/Mac/Mac.cmake
HB_MAC_VERSION=harfbuzz-12.2.0

CMAKE_ADDITIONAL_ARGUMENTS=-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
# Uncomment this to output debug messages while building the library
#CMAKE_ADDITIONAL_ARGUMENTS+=" -DCMAKE_FIND_DEBUG_MODE=ON"
CMAKE_ADDITIONAL_ARGUMENTS+=" -DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}"

MAKE_TARGET=harfbuzz

for ARCH in arm64 x86_64;
do
  "${ENGINE_ROOT}/Build/BatchFiles/RunUAT.command" BuildCMakeLib -TargetPlatform=Mac -TargetArchitecture=${ARCH} -TargetLib=HarfBuzz -TargetLibVersion=${HB_MAC_VERSION} -TargetConfigs=Debug+Release -LibOutputPath=lib -CMakeGenerator=Makefile -CMakeAdditionalArguments="${CMAKE_ADDITIONAL_ARGUMENTS}" -MakeTarget=${MAKE_TARGET} -SkipCreateChangelist
done

pushd "${ENGINE_ROOT}/Source/ThirdParty/HarfBuzz/${HB_MAC_VERSION}/lib/Mac"
mkdir -p Debug Release
lipo -create arm64/Debug/libharfbuzz.a x86_64/Debug/libharfbuzz.a -output Debug/libharfbuzz.a
lipo -create arm64/Release/libharfbuzz.a x86_64/Release/libharfbuzz.a -output Release/libharfbuzz.a
rm -rf arm64 x86_64
popd
