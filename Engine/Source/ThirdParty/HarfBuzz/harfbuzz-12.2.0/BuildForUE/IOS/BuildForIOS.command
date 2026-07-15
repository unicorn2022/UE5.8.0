#!/bin/zsh -eux
# Copyright Epic Games, Inc. All Rights Reserved.
#
# Builds HarfBuzz 12.2.0 static libraries for iOS (device + simulator) against
# ICU 78 and FreeType2-2.14.1. Outputs to:
#   harfbuzz-12.2.0/lib/IOS/{Debug,Release}/libharfbuzz.a
#   harfbuzz-12.2.0/lib/IOS/Simulator/{Debug,Release}/libharfbuzz.a

ENGINE_ROOT=${0:a:h:h:h:h:h:h:h}
THIRD_PARTY_DIRECTORY=${ENGINE_ROOT}/Source/ThirdParty

TOOLCHAIN_FILE=${ENGINE_ROOT}/Source/ThirdParty/CMake/PlatformScripts/IOS/IOS.cmake
HB_IOS_VERSION=harfbuzz-12.2.0

CMAKE_ADDITIONAL_ARGUMENTS=-DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
# Uncomment this to output debug messages while building the library
#CMAKE_ADDITIONAL_ARGUMENTS+=" -DCMAKE_FIND_DEBUG_MODE=ON"
CMAKE_ADDITIONAL_ARGUMENTS+=" -DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}"

MAKE_TARGET=harfbuzz

for ARCH in arm64 x86_64 iossimulator;
do
  "${ENGINE_ROOT}/Build/BatchFiles/RunUAT.command" BuildCMakeLib -TargetPlatform=IOS -TargetArchitecture=${ARCH} -TargetLib=HarfBuzz -TargetLibVersion=${HB_IOS_VERSION} -TargetConfigs=Debug+Release -LibOutputPath=lib -CMakeGenerator=Makefile -CMakeAdditionalArguments="${CMAKE_ADDITIONAL_ARGUMENTS}" -MakeTarget=${MAKE_TARGET} -SkipCreateChangelist
done

pushd "${ENGINE_ROOT}/Source/ThirdParty/HarfBuzz/${HB_IOS_VERSION}/lib/IOS"
mkdir -p Debug Release Simulator Simulator/Debug Simulator/Release
mv arm64/Debug/libharfbuzz.a Debug/libharfbuzz.a
mv arm64/Release/libharfbuzz.a Release/libharfbuzz.a
lipo -create iossimulator/Debug/libharfbuzz.a x86_64/Debug/libharfbuzz.a -output Simulator/Debug/libharfbuzz.a
lipo -create iossimulator/Release/libharfbuzz.a x86_64/Release/libharfbuzz.a -output Simulator/Release/libharfbuzz.a
rm -rf arm64 x86_64 iossimulator
popd
