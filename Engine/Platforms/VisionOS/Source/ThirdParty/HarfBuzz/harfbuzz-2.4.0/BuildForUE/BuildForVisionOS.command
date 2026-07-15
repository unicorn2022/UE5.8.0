#!/bin/zsh -eux
# Copyright Epic Games, Inc. All Rights Reserved.
#
# Builds HarfBuzz 2.4.0 static libraries for visionOS (device + simulator),
# recompiled against ICU 78 headers. Outputs land in the VisionOS platform-extension
# tree:
#   Platforms/VisionOS/Source/ThirdParty/HarfBuzz/harfbuzz-2.4.0/lib-icu78/{Debug,Release}/libharfbuzz{,d}.a
#   Platforms/VisionOS/Source/ThirdParty/HarfBuzz/harfbuzz-2.4.0/lib-icu78/Simulator/{Debug,Release}/libharfbuzz{,d}.a

# We set this to ensure the version of Vision OS we're building for, otherwise we could build the library in a higher version than what UE supports
export XROS_DEPLOYMENT_TARGET=1.0
ENGINE_ROOT=${0:a:h:h:h:h:h:h:h:h}
THIRD_PARTY_DIRECTORY=${ENGINE_ROOT}/Source/ThirdParty
VISIONOS_THIRD_PARTY=${ENGINE_ROOT}/Platforms/VisionOS/Source/ThirdParty

TOOLCHAIN_FILE=${VISIONOS_THIRD_PARTY}/CMake/PlatformScripts/VisionOS.cmake
HB_VISIONOS_VERSION=harfbuzz-2.4.0

CMAKE_ADDITIONAL_ARGUMENTS=-DCMAKE_OSX_DEPLOYMENT_TARGET=1.0
# Uncomment this to output debug messages while building the library
#CMAKE_ADDITIONAL_ARGUMENTS+=" -DCMAKE_FIND_DEBUG_MODE=ON"
CMAKE_ADDITIONAL_ARGUMENTS+=" -DCMAKE_TOOLCHAIN_FILE=${TOOLCHAIN_FILE}"

MAKE_TARGET=harfbuzz

for ARCH in arm64 x86_64 xrsimulator;
do
  "${ENGINE_ROOT}/Build/BatchFiles/RunUAT.command" BuildCMakeLib -TargetPlatform=VisionOS -TargetArchitecture=${ARCH} -TargetLib=HarfBuzz -TargetLibVersion=${HB_VISIONOS_VERSION} -TargetConfigs=Debug+Release -LibOutputPath=lib-icu78 -CMakeGenerator=Makefile -CMakeAdditionalArguments="${CMAKE_ADDITIONAL_ARGUMENTS}" -MakeTarget=${MAKE_TARGET} -SkipCreateChangelist
done

pushd "${VISIONOS_THIRD_PARTY}/HarfBuzz/${HB_VISIONOS_VERSION}/lib-icu78"
mkdir -p Debug Release Simulator Simulator/Debug Simulator/Release
mv arm64/Debug/libharfbuzz.a Debug/libharfbuzz.a
mv arm64/Release/libharfbuzz.a Release/libharfbuzz.a
lipo -create xrsimulator/Debug/libharfbuzz.a x86_64/Debug/libharfbuzz.a -output Simulator/Debug/libharfbuzz.a
lipo -create xrsimulator/Release/libharfbuzz.a x86_64/Release/libharfbuzz.a -output Simulator/Release/libharfbuzz.a
rm -rf arm64 x86_64 xrsimulator
popd
