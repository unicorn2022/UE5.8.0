#!/bin/zsh -eux
# Copyright Epic Games, Inc. All Rights Reserved.
#
# Builds HarfBuzz 2.4.0 static libraries for macOS (arm64 + x86_64), recompiled
# against ICU 78 headers. Outputs universal static libraries to:
#   harfbuzz-2.4.0/lib-icu78/Mac/{Debug,Release}/libharfbuzz{,d}.a

ENGINE_ROOT=${0:a:h:h:h:h:h:h:h}
THIRD_PARTY_DIRECTORY=${ENGINE_ROOT}/Source/ThirdParty

TOOLCHAIN_FILE=${ENGINE_ROOT}/Source/ThirdParty/CMake/PlatformScripts/Mac/Mac.cmake
HB_MAC_VERSION=harfbuzz-2.4.0

CMAKE_ADDITIONAL_ARGUMENTS=-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
# Uncomment this to output debug messages while building the library
#CMAKE_ADDITIONAL_ARGUMENTS+=" -DCMAKE_FIND_DEBUG_MODE=ON"
CMAKE_ADDITIONAL_ARGUMENTS+=" -DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}"

MAKE_TARGET=harfbuzz

for ARCH in arm64 x86_64;
do
  "${ENGINE_ROOT}/Build/BatchFiles/RunUAT.command" BuildCMakeLib -TargetPlatform=Mac -TargetArchitecture=${ARCH} -TargetLib=HarfBuzz -TargetLibVersion=${HB_MAC_VERSION} -TargetConfigs=Debug+Release -LibOutputPath=lib-icu78 -CMakeGenerator=Makefile -CMakeAdditionalArguments="${CMAKE_ADDITIONAL_ARGUMENTS}" -MakeTarget=${MAKE_TARGET} -SkipCreateChangelist
done

pushd "${ENGINE_ROOT}/Source/ThirdParty/HarfBuzz/${HB_MAC_VERSION}/lib-icu78/Mac"
mkdir -p Debug Release
lipo -create arm64/Debug/libharfbuzz.a x86_64/Debug/libharfbuzz.a -output Debug/libharfbuzz.a
lipo -create arm64/Release/libharfbuzz.a x86_64/Release/libharfbuzz.a -output Release/libharfbuzz.a
rm -rf arm64 x86_64
popd
